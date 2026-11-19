#!/usr/bin/env python3
"""Example LogVault client: sends Python log records over TCP as JSON lines.

LogVaultHandler plugs into the standard logging module, so a program sends its logs to LogVault with:

    import logging
    from python_client import LogVaultHandler

    logging.getLogger().addHandler(LogVaultHandler("127.0.0.1", 5170))

Run as a script to produce test traffic (Python 3.8+, standard library only):

    python examples/python_client.py --demo                  # an online shop; stop with Ctrl+C
    python examples/python_client.py --count 10000           # 10,000 numbered lines as fast as possible
    python examples/python_client.py --demo --file shop.log  # ten minutes of shop logs, written at once
"""

import argparse
import json
import logging
import random
import select
import socket
import sys
import time
from datetime import datetime, timezone

DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 5170
DEFAULT_DEMO_RATE = 20.0  # lines per second
DEFAULT_FILE_DURATION = 600.0  # seconds of simulated time that --demo --file writes
BURST_START = 20.0  # seconds into the demo when the "bepaid" payment provider starts timing out
BURST_LENGTH = 15.0

# Python has no TRACE level; LogVault understands the name.
TRACE = 5
logging.addLevelName(TRACE, "TRACE")

# Every LogRecord has these attributes; any other attribute of a record came from `extra`.
_STANDARD_ATTRIBUTES = frozenset(vars(logging.LogRecord("", 0, "", 0, "", None, None))) | {"message", "asctime"}


def iso_utc(created):
    """Formats a time.time() value as ISO 8601 UTC with milliseconds, e.g. 2026-11-19T14:05:01.123Z."""
    msecs = int(created * 1000)
    moment = datetime.fromtimestamp(msecs // 1000, tz=timezone.utc)
    return f"{moment:%Y-%m-%dT%H:%M:%S}.{msecs % 1000:03d}Z"


class JsonLineFormatter(logging.Formatter):
    """Formats a record as one JSON object: time, level, logger, msg, then the `extra` fields."""

    def format(self, record):
        entry = {
            "time": iso_utc(record.created),
            "level": record.levelname,
            "logger": record.name,
            "msg": record.getMessage(),
        }
        for key, value in vars(record).items():
            if key not in _STANDARD_ATTRIBUTES and key not in entry:
                entry[key] = value
        if record.exc_info:
            entry["exception"] = self.formatException(record.exc_info)
        # default=str sends a value that json cannot encode as its text instead of failing.
        return json.dumps(entry, ensure_ascii=False, separators=(",", ":"), default=str)


class LogVaultHandler(logging.Handler):
    """Sends records to LogVault over TCP, one JSON line each.

    The handler connects on the first record and keeps the connection open. If LogVault closed it (a restart,
    say), the handler reconnects and sends the record again. A record that still cannot be sent is reported
    through handleError() and dropped: logging never raises into the application.
    """

    def __init__(self, host=DEFAULT_HOST, port=DEFAULT_PORT, timeout=10.0):
        super().__init__()
        self.host = host
        self.port = port
        self.timeout = timeout
        self.sock = None
        self.setFormatter(JsonLineFormatter())

    def connect(self):
        """Connects now instead of on the first record; raises OSError if LogVault cannot be reached."""
        with self.lock:
            self._ensure_connected()

    def emit(self, record):
        try:
            self._send((self.format(record) + "\n").encode("utf-8"))
        except Exception:
            self.handleError(record)

    def handleError(self, record):
        # One line instead of the default traceback keeps the application's console readable.
        if logging.raiseExceptions and sys.stderr:
            error = sys.exc_info()[1]
            sys.stderr.write(f"LogVaultHandler: a record was not sent to {self.host}:{self.port}: {error}\n")

    def close(self):
        with self.lock:
            self._close_socket()
        super().close()

    def _send(self, data):
        for attempt in (1, 2):
            try:
                self._ensure_connected()
                self.sock.sendall(data)
                return
            except OSError:
                # The connection broke; a fresh connection gets one more chance.
                self._close_socket()
                if attempt == 2:
                    raise

    def _ensure_connected(self):
        # LogVault never sends data, so a readable socket means that the server has closed the connection.
        if self.sock is not None and select.select([self.sock], [], [], 0)[0]:
            self._close_socket()
        if self.sock is None:
            self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)

    def _close_socket(self):
        if self.sock is not None:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


class SimulatedClock(logging.Filter):
    """Stamps records with simulated time instead of the current one: `rate` records per second, starting
    `duration` seconds ago. Used with --demo --file, so a long stretch of logs is written at once."""

    def __init__(self, rate, duration):
        super().__init__()
        self.step = 1.0 / rate
        self.now = time.time() - duration

    def filter(self, record):
        record.created = self.now
        record.msecs = (self.now - int(self.now)) * 1000
        self.now += self.step
        return True


class ShopSimulation:
    """Invents the log records of a small online shop (DESIGN.md section 11.1).

    Four services handle requests: gateway, auth, cart and payment. From BURST_START seconds on, for
    BURST_LENGTH seconds, the "bepaid" payment provider times out and checkouts fail.
    """

    SERVICES = ("gateway", "auth", "cart", "payment")
    PROVIDERS = ("bepaid", "webpay", "assist")
    PATHS = ("/api/products", "/api/cart", "/api/login", "/api/checkout", "/api/orders")

    def __init__(self, seed=None):
        self.random = random.Random(seed)
        self.next_order = 8800

    def next_record(self, elapsed):
        """Returns (service, level, message, args, fields) for the record `elapsed` seconds into the demo."""
        burst = BURST_START <= elapsed < BURST_START + BURST_LENGTH
        weights = (25, 5, 15, 55) if burst else (35, 15, 25, 25)
        service = self.random.choices(self.SERVICES, weights)[0]
        make = {"gateway": self._gateway, "auth": self._auth, "cart": self._cart, "payment": self._payment}
        level, message, args, fields = make[service](burst)
        return service, level, message, args, fields

    def _user(self):
        return self.random.randint(1000, 1099)

    def _gateway(self, burst):
        path = self.random.choice(self.PATHS)
        method = "POST" if path in ("/api/login", "/api/checkout") else "GET"
        if burst and path == "/api/checkout":
            status, latency = 504, self.random.randint(3000, 5000)
        else:
            status = self.random.choices((200, 201, 304, 400, 404, 500), (70, 8, 10, 5, 5, 2))[0]
            latency = self.random.randint(5, 180)
        level = logging.ERROR if status >= 500 else logging.WARNING if status >= 400 else logging.INFO
        fields = {
            "http": {"method": method, "path": path, "status": status},
            "latency_ms": latency,
            "user_id": self._user(),
        }
        return level, "%s %s %d in %d ms", (method, path, status, latency), fields

    def _auth(self, burst):
        user = self._user()
        roll = self.random.random()
        if roll < 0.07:
            return logging.WARNING, "Invalid password for user %d", (user,), {"user_id": user}
        if roll < 0.35:
            return logging.DEBUG, "Session token refreshed for user %d", (user,), {"user_id": user}
        method = self.random.choice(("password", "google"))
        return logging.INFO, "User %d logged in", (user,), {"user_id": user, "auth_method": method}

    def _cart(self, burst):
        user = self._user()
        roll = self.random.random()
        if roll < 0.04:
            return logging.WARNING, "Cart not found for user %d", (user,), {"user_id": user}
        if roll < 0.25:
            took = self.random.randint(1, 9)
            return TRACE, "Cart of user %d serialized in %d ms", (user, took), {"user_id": user}
        if roll < 0.45:
            return logging.DEBUG, "Cache hit for cart:%d", (user,), {"user_id": user, "cache": "redis"}
        item = self.random.randint(1, 500)
        fields = {"user_id": user, "item_id": item, "quantity": self.random.randint(1, 3)}
        return logging.INFO, "Item %d added to cart of user %d", (item, user), fields

    def _payment(self, burst):
        self.next_order += 1
        order = self.next_order
        provider = "bepaid" if burst and self.random.random() < 0.8 else self.random.choice(self.PROVIDERS)
        fields = {"provider": provider, "order_id": order, "user_id": self._user()}
        if burst and provider == "bepaid":
            if self.random.random() < 0.02:
                return logging.CRITICAL, "Circuit breaker opened for provider %s", (provider,), fields
            latency = self.random.randint(3000, 5000)
            fields["latency_ms"] = latency
            return logging.ERROR, "Payment provider timeout after %d ms", (latency,), fields
        latency = self.random.randint(150, 900)
        fields.update(latency_ms=latency, amount=round(self.random.uniform(5, 300), 2), currency="BYN")
        if self.random.random() < 0.03:
            return logging.ERROR, "Card declined for order %d", (order,), fields
        return logging.INFO, "Payment for order %d authorized by %s in %d ms", (order, provider, latency), fields


def wait_until(started, offset):
    """Sleeps until `offset` seconds after `started` (a time.monotonic() value)."""
    delay = started + offset - time.monotonic()
    if delay > 0:
        time.sleep(delay)


def run_count(count, rate):
    """Logs `count` numbered lines, paced to `rate` per second if given; returns how many were logged."""
    logger = logging.getLogger("shop.loadtest")
    started = time.monotonic()
    logged = 0
    try:
        for number in range(1, count + 1):
            if rate:
                wait_until(started, (number - 1) / rate)
            logger.info("Test line %d of %d", number, count, extra={"service": "loadtest", "seq": number})
            logged = number
    except KeyboardInterrupt:
        pass
    return logged


def run_demo(rate, duration, realtime):
    """Logs the shop's records at `rate` per second for `duration` seconds (until Ctrl+C if None).
    Without `realtime` it does not wait, for a file whose records carry simulated time."""
    shop = ShopSimulation()
    started = time.monotonic()
    logged = 0
    try:
        while duration is None or logged < duration * rate:
            elapsed = logged / rate
            if realtime:
                wait_until(started, elapsed)
            service, level, message, args, fields = shop.next_record(elapsed)
            fields["service"] = service
            logging.getLogger(f"shop.{service}").log(level, message, *args, extra=fields)
            logged += 1
    except KeyboardInterrupt:
        pass
    return logged


def parse_arguments(argv):
    parser = argparse.ArgumentParser(description="Send test log records to LogVault over TCP, or write them to a file.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--demo", action="store_true", help="simulate an online shop; stop with Ctrl+C")
    mode.add_argument("--count", type=int, metavar="N", help="send N numbered lines, then exit")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"LogVault address (default {DEFAULT_HOST})")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"LogVault port (default {DEFAULT_PORT})")
    parser.add_argument("--rate", type=float, metavar="LINES",
                        help=f"lines per second (default: {DEFAULT_DEMO_RATE:g} for --demo, "
                             "as fast as possible for --count)")
    parser.add_argument("--duration", type=float, metavar="SECONDS",
                        help=f"length of the demo (default: until Ctrl+C; {DEFAULT_FILE_DURATION:g} with --file)")
    parser.add_argument("--file", metavar="PATH",
                        help="write the lines to a file instead of sending them; with --demo the time is "
                             "simulated, so the whole file is written at once (an import sample)")
    args = parser.parse_args(argv)
    if args.count is not None and args.count < 0:
        parser.error("--count must not be negative")
    if args.rate is not None and args.rate <= 0:
        parser.error("--rate must be positive")
    if args.duration is not None and args.duration <= 0:
        parser.error("--duration must be positive")
    if not 0 < args.port < 65536:
        parser.error("--port must be between 1 and 65535")
    return args


def main(argv=None):
    args = parse_arguments(argv)
    rate = args.rate or DEFAULT_DEMO_RATE
    duration = args.duration or (DEFAULT_FILE_DURATION if args.file else None)

    if args.file:
        handler = logging.FileHandler(args.file, mode="w", encoding="utf-8")
        handler.setFormatter(JsonLineFormatter())
        if args.demo:
            handler.addFilter(SimulatedClock(rate, duration))
        target = args.file
    else:
        handler = LogVaultHandler(args.host, args.port)
        target = f"{args.host}:{args.port}"
        try:
            handler.connect()
        except OSError as error:
            print(f"Cannot connect to LogVault at {target}: {error}. Is the receiver started?", file=sys.stderr)
            return 1

    logger = logging.getLogger("shop")
    logger.setLevel(TRACE)
    logger.propagate = False
    logger.addHandler(handler)
    started = time.monotonic()
    try:
        if args.demo:
            logged = run_demo(rate, duration, realtime=not args.file)
        else:
            logged = run_count(args.count, args.rate)
    finally:
        logger.removeHandler(handler)
        handler.close()
    action = "Wrote" if args.file else "Sent"
    print(f"{action} {logged} lines to {target} in {time.monotonic() - started:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
