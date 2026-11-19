# LogVault — Design Document (v0.4)

> **Coursework project** for the "System Programming" course (3rd year).
> **Topic:** software for storing and organizing logging results.
> **What it is:** a Windows desktop application (C++20, Qt 6) that receives logs over the network (TCP) and from files,
> stores them in a database, and lets the user search, filter and analyze them conveniently.
> **Size:** about 4–5k lines of product code, tests excluded. This is a guideline, not a limit: reliability and quality come first.
> **Language:** everything in the repository is in English — UI text, code comments, documentation, commit messages.
> **Status:** v0.4, scope agreed. Development goes one stage per session (§9); progress is tracked in `docs/PROGRESS.md`.

## 0. Rules for agents

- This document is the single source of truth. Before a stage, read the sections listed for it in §9.
- Implement only the features from §1.2. If something is missing, choose the simplest option consistent with this
  document and note it as `ASSUMPTION: …` in the commit message.
- Line counts are a guideline. Never cut error handling, checks or tests to save lines.
- No third-party libraries besides Qt without the author's approval.
- A stage is done when the build has no warnings, tests pass, the stage's manual check passes,
  and the work is committed and pushed.

### 0.1. Quality over size

1. **Errors are never lost.** Check the result of every network, file and database operation (`listen`, `open`, `exec`,
   `commit`, …). An error is either handled or shown to the user as a clear message — never silently ignored.
2. **Bad input never crashes the app.** Corrupted or oversized lines, a connection dropped mid-line, an empty or broken
   file, a busy port, a damaged database — the app neither crashes nor hangs.
3. **The UI never freezes.** Anything that can take longer than ~100 ms (receiving, import, search, export, deletion)
   runs in worker threads.
4. **Clean shutdown.** On close, the server stops, the pending batch is written to the database,
   and all threads finish (`quit()` + `wait()`).
5. **No data loss.** An accepted line either ends up in the database (a batch is one transaction) or the user sees an error.
6. **Clean code.** No warnings, clear names, short functions, no duplication; the core is covered by tests;
   comments follow §7.3.

---

## 1. Product

### 1.1. Why

Every program writes logs. Once there are several programs, logs in different formats end up in different places,
and answering "what happened at 14:05 when payments failed?" becomes hard. Server-side systems (OpenObserve, Graylog,
Elasticsearch + Kibana) solve this: applications send logs over the network to a collector, which parses them, stores
them and provides search and charts. LogVault does the same on a single computer, as a regular desktop application.

```
 application (e.g. a Python web server)
   └─ logger ──── TCP ────▶ ┌──────────────────── LogVault ────────────────────────┐
 log files ──── import ───▶ │ intake → parsing → batched writes → database (SQLite) │ ──▶ search, filters,
                            └────────────────────────────────────────────────────────┘     histogram, statistics
```

### 1.2. Features

**Databases and sources**

| ID | Feature | Priority |
|---|---|---|
| F1 | Create or open a database (`*.lvdb` file); list of recent databases. | MUST |
| F2 | Sources are created automatically (file name, JSON field `service`/`source`/`app`, default TCP source). Source list with record counts; rename; delete together with its records. | MUST |
| F3 | Delete records older than a chosen date — in all sources or in the selected one. | SHOULD |

**Receiving logs**

| ID | Feature | Priority |
|---|---|---|
| F4 | TCP server: port, address (this computer only / whole network), start and stop, autostart on launch. | MUST |
| F5 | "Receiver" panel: server state, list of connections (address, connected at, lines received), receive rate. | MUST |
| F6 | Import one or several files (menu or drag & drop onto the window) in the background, with progress and cancel. Summary: records added, lines not parsed. | MUST |
| F7 | Line parsing: JSON or plain text `<time> <level> <message>`; multi-line stack traces; levels mapped to one scale. | MUST |

**Viewing, search, analysis**

| ID | Feature | Priority |
|---|---|---|
| F8 | Records table: time, level (colored), source, message; newest first; up to N rows (setting, default 10,000). Columns can be hidden, reordered and resized. | MUST |
| F9 | Details panel: all fields of the record and the raw line; copy; "Search for this value" / "Exclude this value" add a condition to the search line. | MUST |
| F10 | Filters: levels (toggle buttons), sources (selection in the side panel, several allowed), period (presets and a custom range). | MUST |
| F11 | Search line: words and phrases are matched in the message, `field:value` matches JSON fields, a leading `-` excludes. History of recent searches. | MUST |
| F12 | Time histogram of record counts, stacked by level; tooltip on hover; drag-selecting a range zooms into it. | MUST |
| F13 | Statistics: group the results by level, source, similar messages (digits replaced with `#`) or any JSON field; count and share; double-click adds the group to the filter. | MUST |
| F14 | Live mode: new records appear automatically (refresh at most once per second). | MUST |

**Other**

| ID | Feature | Priority |
|---|---|---|
| F15 | Export the current search results to CSV or JSON Lines (in the background). | SHOULD |
| F16 | Settings: TCP (port, address, autostart, default source), display time zone (local/UTC), theme (system/light/dark), max rows in the table. | SHOULD |
| F17 | Window size, panel layout and columns are remembered between launches. | SHOULD |

### 1.3. Out of scope

A custom query language with parentheses and aggregations, message clustering algorithms, formats other than JSON and
plain text (syslog, nginx, …), UDP and HTTP intake, saved searches, bookmarks, alerts, dashboards, multiple search tabs,
users and permissions, a web UI, an installer, CI/CD.

### 1.4. Quality targets

| Aspect | Target |
|---|---|
| Data volume | up to ~1M records per database |
| File import | ≥ 20,000 lines/s |
| TCP intake | ≥ 5,000 lines/s without loss, up to 50 concurrent connections |
| Search | usually ≤ 1 s on 1M records; the UI stays responsive while searching |
| Reliability | a crash during intake does not corrupt the database; committed batches survive |

---

## 2. User interface

### 2.1. Main window

```
┌ LogVault — demo.lvdb ─────────────────────────────────────────────────────────────────────────┐
│ File  Data  Receiver  View  Help                                                              │
│ [Open] [Import] [Export] │ [▶ Start receiver :5170] │ [● Live] [⟳ Refresh]                      │
├────────────────────┬────────────────────────────────────────────────────────┬─────────────────┤
│ SOURCES            │ Levels  [TRACE][DEBUG][INFO][WARN][ERROR][FATAL][ ? ]  │ RECORD DETAILS  │
│ ▸ All      12,345  │ Period  [All time             ▾]                       │ Time            │
│   payment   4,210  │ Search  [timeout provider:bepaid -env:test   ] [Search]│  14:05:01.123   │
│   cart      3,102  ├────────────────────────────────────────────────────────┤ Level    ERROR  │
│   my-server   911  │ ▁▂▂▃▂▁▁▂█▇▃▁▁▁▂▂   histogram: color = level,            │ Source  payment │
│                    │                    drag to zoom in                     │ provider bepaid │
├────────────────────┼────────────────────────────────────────────────────────┤ order_id 8812   │
│ RECEIVER           │ [Records 1,234] [Statistics]                           │ ─────────────── │
│ ● :5170 running    │ Time          Level    Source    Message               │ Raw line        │
│ 127.0.0.1:53122 812│ 14:05:01.123  ERROR    payment   Payment provider time…│ {"time":"2026-… │
│ 10.0.0.7:41000   99│ 14:05:00.950  WARN     cart      Cart not found        │                 │
│ 150 lines/s        │ …                                                      │                 │
├────────────────────┴────────────────────────────────────────────────────────┴─────────────────┤
│ Found 1,234 of 12,345 · 0.18 s · Importing app.log: 45% ██████░░░░░ [Cancel]                   │
└───────────────────────────────────────────────────────────────────────────────────────────────┘
```

- "Sources", "Receiver" and "Record details" are dock panels (`QDockWidget`): they can be moved, closed and turned
  back on from the View menu.
- The central area, top to bottom: the filter bar, the histogram, the "Records" and "Statistics" tabs.
- While no database is open, the center shows a placeholder with "New database", "Open database" and the recent list.

### 2.2. Elements and behavior

| Element | Implementation | Behavior |
|---|---|---|
| Sources | `QListWidget` | First item "All". Selecting one or several (Ctrl/Shift) filters by source. Context menu: rename, delete. Counts refresh after each search and in live mode. |
| Receiver | `QWidget` + `QTableView` | Server state (address, port, running/stopped/error), start/stop button, connections table, receive rate (lines/s). |
| Levels | checkable `QToolButton`s in level colors | All on by default. Click toggles a level; Ctrl+click keeps only that level. |
| Period | `QComboBox` | Last 15 minutes, Last hour, Last 24 hours, Last 7 days, All time (default), Custom range… (dialog with two `QDateTimeEdit`s). |
| Search | `QLineEdit` + `QCompleter` | Syntax in §6.2. Enter or "Search" runs the search; history of the last 20 searches. |
| Histogram | custom `QWidget` painted with `QPainter` | Bars stacked by level, tooltip (interval and counts). Drag-select → "Custom range" period; Esc restores the previous period. |
| Records | `QTableView` + `RecordTableModel` | Level colors, monospace message (first line only). Header menu shows/hides columns. Ctrl+C copies the raw lines of the selected records. |
| Statistics | `QComboBox` + `QTableView` | "Group by: level / source / similar messages / field…" (plus a field name box). Columns: value, count, share. Double-click adds the group to the filter. |
| Record details | `QTreeWidget` + `QPlainTextEdit` | "name — value" pairs, raw line below. Field context menu: "Search for this value", "Exclude this value", "Copy". Double-click on a field = "Search for this value". For level and source these actions change the level buttons and source selection instead of the search line. |
| Status bar | `QStatusBar` | Found / total, search time, import or export progress with a "Cancel" button, live indicator. |

### 2.3. Menus and shortcuts

| Menu | Items |
|---|---|
| File | New Database… (Ctrl+N), Open Database… (Ctrl+O), Recent ▸, Import Files… (Ctrl+I), Export Results… (Ctrl+E), Exit |
| Data | Rename Source…, Delete Source…, Delete Records Older Than… |
| Receiver | Start / Stop (Ctrl+R), Receiver Settings… |
| View | Panels ▸ (Sources, Receiver, Record Details), Live Mode (Ctrl+L), Refresh (F5), Theme ▸, Settings… |
| Help | About LogVault |

Ctrl+F focuses the search line; Enter searches; Esc cancels the histogram selection.

### 2.4. Look and feel

- `Fusion` style; theme: system (Qt 6.5+ follows the Windows theme), light or dark — via `QPalette`.
- Level colors are constants in one place (`gui/Theme`), shared by the table, the level buttons and the histogram:
  TRACE `#8A8F98`, DEBUG `#5B7083`, INFO `#2E7D32`, WARN `#E0A100`, ERROR `#D32F2F`, FATAL `#7B1FA2`, UNKNOWN `#9E9E9E`.
- Monospace font for messages and raw lines; numbers with thousands separators.
- Icons: standard `QStyle::standardIcon`; only the application icon is custom.

---

## 3. Architecture

### 3.1. Layers

| Layer | Folder | Contents | Qt modules |
|---|---|---|---|
| `core` | `src/core` | Data model, line parsing, database, search, TCP intake, import, export. No windows or widgets. | Core, Sql, Network, Concurrent |
| `gui` | `src/gui` | Main window, panels, dialogs, table models, histogram. | Widgets |

`core` is built as the static library `lv_core`, used by the application and the tests. `core` must never include
`QtWidgets`: this keeps the logic testable without a window and the UI layer thin.

### 3.2. Classes

| Class | Layer | Responsibility |
|---|---|---|
| `Record`, `Level` | core | A log record and the level scale (§4.1). |
| `TimeParser` | core | Parses a timestamp from text or a number (§5.4). |
| `LineParser` | core | Line → `Record`: JSON or plain text, fields, `msg_key`. Stateless. |
| `Database` | core | Opens a connection in the current thread, applies SQLite settings, creates the schema. |
| `LogStore` | core | All SQL: batch insert, sources, search, histogram, statistics, deletion. The only place with SQL. |
| `SearchQuery` | core | Filters + search line → `WHERE` clause and bound values (§6). |
| `SearchService` | core | Runs searches in a worker thread and delivers results via a signal. |
| `Ingestor` | core | Collects received lines into batches and writes them to the database. |
| `TcpServer` | core | Listens on a port, manages connections, splits the byte stream into lines. |
| `FileImporter` | core | Imports files in a worker thread: progress, cancel, summary. |
| `Exporter` | core | Exports results to CSV / JSON Lines in a worker thread. |
| `MainWindow` | gui | Menus, panels, wiring between widgets and `core` objects. No business logic. |
| `SourcesPanel`, `ReceiverPanel`, `FilterBar`, `HistogramWidget`, `RecordTableModel`, `RecordDetailsPanel`, `StatsView` | gui | The elements from §2.2. |
| `SettingsDialog`, `PeriodDialog`, `Theme` | gui | Settings, custom period, colors and palettes. |

### 3.3. Threads

```
 GUI thread ─── window and models; reacts to signals
   │   (signals and slots: Qt passes them between threads through the event queue)
   ├──▶ intake thread (QThread): TcpServer → Ingestor → batched writes        [own DB connection]
   ├──▶ thread pool (QThreadPool): FileImporter → parsing → 1,000-record transactions [own DB connection]
   ├──▶ thread pool: SearchService → rows, counts, histogram, statistics      [own DB connection, read-only]
   └──▶ thread pool: Exporter, deleting old records                           [own DB connection]
```

Rules:
- **Every thread has its own database connection** (`QSqlDatabase` with a unique name). Qt forbids using a connection
  from another thread.
- There can be two writers (intake and import). SQLite serializes their writes; `PRAGMA busy_timeout = 5000` makes
  a writer wait instead of failing with "database is locked". Transactions are short.
- WAL mode: reads do not block writes, so searching works during intake.
- Threads communicate only through signals and slots; cancellation uses a `std::atomic<bool>` flag.
  No shared mutable data without protection.
- Every search gets a number (generation). If the user starts a new search, results of the old one are dropped.
- Shutdown order: stop the server → `Ingestor` writes the pending batch → cancel import and export → wait for the
  thread pool → `quit()` + `wait()` the intake thread → close connections.

### 3.4. The path of one log line (under the hood)

```
Python: logging.error("Payment timeout")  →  LogVaultHandler  →  socket.sendall(b'{"level":"error",...}\n')
   ↓  the Windows TCP/IP stack delivers the bytes into the socket buffer on the LogVault side
TcpServer (intake thread): readyRead signal → bytes appended to this client's buffer → split on '\n'
   ↓  a line may arrive in pieces, or several lines at once — the buffer handles both
Ingestor: line → batch; every 200 ms or 500 lines → LineParser → one INSERT transaction in SQLite
   ↓  recordsWritten(n) signal → GUI thread event queue
MainWindow: in live mode starts a search (at most once per second) → table and histogram are updated
```

OS mechanisms involved (useful for the defense):

| Mechanism | Where | What it does |
|---|---|---|
| TCP sockets (Winsock) | `TcpServer` via QtNetwork | `listen`/`accept`/`recv`: accepting connections and bytes |
| Event-driven I/O | intake thread event loop | the OS reports which socket has data; one thread serves all connections |
| Threads | `QThread`, `QThreadPool` | intake, import, search and export run in parallel with the UI |
| Cross-thread event queue | Qt signals/slots | safe data hand-off between threads without manual locks |
| File locking and journaling | SQLite (WAL, transactions) | database integrity under concurrent writes and crashes |
| Atomic variables | cancel flags, counters | safe exchange of simple values between threads |

### 3.5. Decisions (for the defense)

| Decision | Why |
|---|---|
| C++20 + Qt 6 Widgets | The industry standard for desktop C++; ready-made tables (Model/View), docks, dialogs |
| Qt only, no third-party libraries | One framework: fewer dependencies, simpler build; networking, SQL, JSON, threads and tests are all in Qt |
| SQLite via QtSql | A single-file database without a server; transactions, indexes, JSON functions |
| "One line = one record" over TCP | The simplest protocol: works from any language, `netcat`, PowerShell, Fluent Bit (`tcp` output) |
| Intake in its own thread | Receiving does not depend on what the UI is doing |
| Batched writes | A transaction per line is slow (each commit is flushed to disk); a batch is fast and atomic |
| Background search with generations | The UI never freezes; stale results never overwrite fresh ones |
| JSON fields in one column + `json_extract` | Logs of any structure need no schema changes |
| Search line without a grammar | Simple splitting into words and `field:value`; any input is valid, so there are no syntax errors |

---

## 4. Data

### 4.1. Record

```cpp
enum class Level : int { Unknown = 0, Trace, Debug, Info, Warn, Error, Fatal };   // values are stored in the DB

struct Record {
    qint64  id = 0;
    qint64  ts = 0;              // milliseconds since 1970-01-01, UTC
    Level   level = Level::Unknown;
    QString source;
    QString message;
    QString fields;              // flat JSON object with the remaining fields; empty if none
    QString raw;                 // original line (including joined stack trace lines)
};
```

### 4.2. Database schema

```sql
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL                       -- schema_version = 1
);

CREATE TABLE IF NOT EXISTS sources (
    id         INTEGER PRIMARY KEY,
    name       TEXT    NOT NULL UNIQUE,
    created_at INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS records (
    id        INTEGER PRIMARY KEY,
    ts        INTEGER NOT NULL,               -- ms, UTC
    level     INTEGER NOT NULL,               -- 0..6, see Level
    source_id INTEGER NOT NULL REFERENCES sources(id) ON DELETE CASCADE,
    message   TEXT    NOT NULL,
    msg_key   TEXT    NOT NULL,               -- message with digit runs replaced by '#'
    fields    TEXT,                           -- JSON object or NULL
    raw       TEXT    NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_records_ts        ON records(ts);
CREATE INDEX IF NOT EXISTS idx_records_source_ts ON records(source_id, ts);
CREATE INDEX IF NOT EXISTS idx_records_level_ts  ON records(level, ts);
```

Per-connection settings:

```sql
PRAGMA journal_mode = WAL;      -- reads do not block writes
PRAGMA synchronous  = NORMAL;   -- safe in WAL mode and much faster than FULL
PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;     -- wait up to 5 s if another writer holds the lock
```

- On open, `meta.schema_version` is checked: no tables — create them; a version newer than the app — a clear error.
- Nested JSON objects are flattened into dotted field names during parsing: `{"http":{"status":500}}` → `http.status = 500`.
- Estimate: ≈ 0.6 KB per record, 1M records ≈ 600 MB.

---

## 5. Receiving logs

### 5.1. TCP protocol

- A client connects to `127.0.0.1:5170` (the port is configurable) and sends UTF-8 lines, each ending with `\n`;
  a `\r` before it is dropped. One line = one record. The server sends nothing back.
- A line is a JSON object or plain text (§5.4).
- Lines longer than 64 KB are truncated (the record gets the field `_truncated: true`).
- A connection may stay open for a long time (a logger keeps it open) or be opened for each batch of lines.
- Address: "this computer only" (`127.0.0.1`, default) or "whole network" (`0.0.0.0`, needs a firewall permission).

Intake can be checked without writing any code, from PowerShell:

```powershell
$c = [Net.Sockets.TcpClient]::new('127.0.0.1', 5170); $w = [IO.StreamWriter]::new($c.GetStream())
$w.WriteLine('{"level":"error","service":"test","msg":"Hello from PowerShell"}'); $w.Flush(); $c.Close()
```

### 5.2. Server (`TcpServer`, intake thread)

- `QTcpServer::listen(address, port)`; a failure (port busy, no permission) → `errorOccurred(message)` signal and the
  "error" state.
- New connection → its own buffer (`QByteArray`) and line counter; `clientConnected(id, address)` signal.
  Above 50 connections a new one is closed immediately.
- `readyRead` → append the bytes to the buffer → cut out all complete lines → pass them to `Ingestor` with the default
  source. If the buffer reaches 64 KB without `\n`, it is passed on as a truncated line.
- Disconnect → the rest of the buffer (if not empty) becomes the last line → `clientDisconnected(id)` → `deleteLater()`.
- Once per second → `statsChanged(connections, lines per second, total lines)` for the Receiver panel.
- Stop: close the listening socket and all connections, pass the buffer leftovers to `Ingestor`.

### 5.3. File import (`FileImporter`, thread pool)

- Files are chosen in a `QFileDialog` (several allowed) or dropped onto the window. The source for each file is the
  file name without extension; a small dialog before the import lets the user change it or pick an existing source.
- Reading line by line (`QFile` + `readLine`); a UTF-8 BOM at the start is skipped.
- A line starting with a space or a tab is appended to the previous record (this joins stack traces).
- Writing in transactions of 1,000 records. Progress (percent of bytes) is signaled at most 10 times per second.
- Cancel sets a flag; already committed transactions stay in the database, and the summary says so.
- Summary: "Added N records, M lines not parsed, took T" (unparsed lines are still stored, see §5.4).

### 5.4. Line parsing (`LineParser`)

1. The line starts with `{` → JSON (`QJsonDocument::fromJson`). Special fields are taken from the first matching name:

   | Field | Names (case-insensitive) |
   |---|---|
   | time | `time`, `timestamp`, `ts`, `@timestamp` |
   | level | `level`, `severity`, `lvl` |
   | message | `message`, `msg` |
   | source | `source`, `service`, `app`; otherwise the file's source or the default TCP source |

   All other fields (flattened with dots) go to `fields`.
2. Otherwise — plain text: a timestamp is looked for at the start of the line, then a level (a word or in brackets:
   `ERROR`, `[ERROR]`), and the rest is the message.
3. Nothing recognized (broken JSON, no timestamp) → the whole line becomes the message, the level is `Unknown`, the time
   is the moment of receipt, and the fields get `_parse_error: true`. **Lines are never lost.**

**Timestamps:** ISO 8601 (`2026-11-19T14:05:01Z`, `2026-11-19T14:05:01.123+03:00`); `2026-11-19 14:05:01`,
`2026-11-19 14:05:01,123`, `2026-11-19 14:05:01.123` (no zone — local time); in JSON also a number of seconds
(10 digits) or milliseconds (13 digits).

**Levels** (case-insensitive): `trace`; `debug`/`dbg`; `info`/`information`/`notice`; `warn`/`warning`;
`error`/`err`; `fatal`/`critical`/`crit`/`panic`; anything else — `Unknown`.

**`msg_key`:** every run of digits is replaced with `#`, and the result is cut to 200 characters:
`Timeout after 3000 ms for order 8812` → `Timeout after # ms for order #`. This puts similar messages into one group.

### 5.5. Writing (`Ingestor`, intake thread)

- Collects lines and flushes a batch at 500 lines or 200 ms after the first line of the batch, whichever comes first.
- A batch is one transaction: parse → new sources (`INSERT OR IGNORE`, cache "name → id") → `INSERT` the records with
  a prepared query → `COMMIT`. On error → `ROLLBACK` and a signal with the error message.
- After each batch → `recordsWritten(n)` signal.

---

## 6. Search and analysis

### 6.1. Request and result

```cpp
enum class StatsMode { None, Level, Source, Message, Field };

struct SearchRequest {
    QList<Level>  levels;            // enabled levels
    QList<qint64> sourceIds;         // empty = all sources
    qint64  from = 0, to = 0;        // ms UTC; 0 = unbounded
    QString text;                    // search line (§6.2)
    int     maxRows = 10000;
    StatsMode statsMode = StatsMode::None;   // statistics are computed only while the Statistics tab is open
    QString   statsField;                    // for StatsMode::Field
};

struct SearchResult {
    quint64 generation = 0;          // search number; stale results are dropped
    QVector<Record> rows;            // at most maxRows, newest first
    qint64  total = 0;               // total number of matches
    Histogram histogram;             // start, step and per-level counts for each bar
    QVector<StatsRow> stats;         // value, count
    qint64  elapsedMs = 0;
    QString error;                   // empty = success
};
```

`SearchService::search(request)` starts the work in the thread pool and returns the search number;
the result arrives through the `finished(SearchResult)` signal.

### 6.2. Search line

The line is split into parts by spaces (a quoted phrase is one part). All parts are combined with AND.
Any input is valid: there are no syntax errors; a part that cannot be interpreted is searched as plain text.

| Example | Meaning | SQL (simplified) |
|---|---|---|
| `timeout` | the message contains the word (case-insensitive for Latin letters) | `r.message LIKE '%timeout%'` |
| `"connection refused"` | the message contains the phrase | `r.message LIKE '%connection refused%'` |
| `provider:bepaid` | a JSON field equals the value (case-insensitive) | `CAST(json_extract(r.fields, '$."provider"') AS TEXT) = ? COLLATE NOCASE` |
| `user:adm*` | field value by pattern, `*` = any characters | `… LIKE 'adm%'` |
| `order_id:*` | the record has the field | `json_extract(…) IS NOT NULL` |
| `-debug`, `-env:test` | exclude | `NOT coalesce(<condition>, 0)` |

- `CAST(... AS TEXT)` makes `status:500` match both the number `500` and the string `"500"`.
- `NOT coalesce(…, 0)` makes a record **without** the `env` field match `-env:test` (as in Kibana).
- **Values reach SQL only through bound parameters (`?`)** — protection against SQL injection. A field name may contain
  only `A–Z a–z 0–9 _ . @ -`; otherwise the part is treated as plain text. `%` and `_` in values are escaped.
- Levels, sources and the period come from the UI controls (§2.2) and are added to the condition automatically.

### 6.3. Database queries (`LogStore`)

```sql
-- records (newest first)
SELECT r.id, r.ts, r.level, s.name, r.message, r.fields, r.raw
FROM records r JOIN sources s ON s.id = r.source_id
WHERE <F> ORDER BY r.ts DESC, r.id DESC LIMIT :maxRows;

-- total matches and their time span (for "All time" — to pick the histogram step)
SELECT count(*), min(r.ts), max(r.ts) FROM records r WHERE <F>;

-- histogram
SELECT (r.ts - :from) / :step AS bucket, r.level, count(*)
FROM records r WHERE <F> GROUP BY bucket, r.level;

-- statistics: by level / source / similar messages / field
SELECT r.level, count(*)   FROM records r WHERE <F> GROUP BY 1 ORDER BY 2 DESC;
SELECT s.name,  count(*)   FROM records r JOIN sources s ON s.id = r.source_id WHERE <F> GROUP BY 1 ORDER BY 2 DESC;
SELECT r.msg_key, count(*) FROM records r WHERE <F> GROUP BY 1 ORDER BY 2 DESC LIMIT 100;
SELECT CAST(json_extract(r.fields, :path) AS TEXT), count(*)
                           FROM records r WHERE <F> GROUP BY 1 ORDER BY 2 DESC LIMIT 100;   -- NULL = "(no field)"
```

`<F>` is the condition built from the UI filters and the search line (§6.2).

### 6.4. Histogram

- The step is the first of 1 s, 5 s, 15 s, 1 min, 5 min, 15 min, 1 h, 3 h, 12 h, 1 day, 7 days, 30 days that gives
  at most 80 bars. The start is aligned down to a step boundary.
- Bars are stacked by level using the `Theme` colors; the tooltip shows the interval and per-level counts.
- Drag-select → the "Custom range" period from the selection bounds → a new search. Esc restores the previous period.

### 6.5. Statistics

- The Statistics tab: "Group by: level / source / similar messages / field…".
- Table: value, count, share (percent of matches). Levels and sources are shown by name, not by number.
- Double-click: level → keep only that level; source → select that source; field → add `field:value` to the search line;
  similar message → add the longest `#`-free part of the pattern, in quotes, to the search line.

### 6.6. Live mode

- Toggled by the "Live" button (Ctrl+L). A `recordsWritten` signal starts a search, but at most once per second and
  never while the previous search is still running.
- Relative periods ("last 15 minutes", etc.) are recalculated from the current time on every search.
- If the user scrolls the table down or selects a record, live mode pauses (indicator in the status bar) so that the table
  does not jump under the cursor; scrolling back to the top resumes it.

### 6.7. Export and deletion

- **Export** writes all records of the current search (not only the rows shown in the table), streaming, in the
  background, with progress and cancel. CSV: UTF-8 with BOM, RFC 4180 quoting; the separator is the Windows list
  separator from the regional settings (`HKCU\Control Panel\International\sList`, read via `QSettings`; `,` if missing) —
  that is what Excel expects when a CSV file is opened by double-click. Columns: time, level, source, message, fields.
  JSON Lines: one object per record.
- **Deletion** (a source or records older than a date) — after confirmation, in the background, in chunks of 10,000
  records, so the database is never locked for long.

---

## 7. Code

### 7.1. Repository layout

```
logvault/
├── CMakeLists.txt
├── CMakePresets.json
├── src/
│   ├── core/       Record, TimeParser, LineParser, Database, LogStore, SearchQuery, SearchService,
│   │               Ingestor, TcpServer, FileImporter, Exporter
│   └── gui/        main.cpp, MainWindow, SourcesPanel, ReceiverPanel, FilterBar, PeriodDialog,
│                   HistogramWidget, RecordTableModel, RecordDetailsPanel, StatsView, SettingsDialog,
│                   Theme, resources.qrc (application icon)
├── tests/          tst_*.cpp (Qt Test)
├── examples/       python_client.py
├── docs/           DESIGN.md, PROGRESS.md
├── CLAUDE.md       instructions for AI agents
├── .clang-format  .gitignore  .gitattributes
└── README.md
```

### 7.2. Size guideline

| Part | Classes | Lines ≈ |
|---|---|---|
| Data and parsing | `Record`, `TimeParser`, `LineParser` | 450 |
| Database | `Database`, `LogStore` | 570 |
| Search | `SearchQuery`, `SearchService` | 360 |
| Intake, import, export | `Ingestor`, `TcpServer`, `FileImporter`, `Exporter` | 680 |
| Main window | `main.cpp`, `MainWindow` | 600 |
| Panels | `SourcesPanel`, `ReceiverPanel`, `FilterBar`, `RecordDetailsPanel`, `StatsView` | 910 |
| Table and histogram | `RecordTableModel`, `HistogramWidget` | 410 |
| Dialogs and theming | `PeriodDialog`, `SettingsDialog`, `Theme` | 310 |
| **Total** | | **≈ 4,300** — a guideline, not a limit |

### 7.3. Code style

- **Language: English everywhere** — identifiers, comments, UI text, documentation, commit messages.
- C++20. Classes `PascalCase`, methods and variables `camelCase`, members `m_name`, constants `kName`.
- Qt types inside the program (`QString`, `QVector`, `QDateTime`); time in data is `qint64` ms UTC.
- Ownership: every `QObject` has a parent, everything else is held by `std::unique_ptr`; no naked `delete`.
  Sockets are destroyed with `deleteLater()`.
- Signals and slots only with the pointer-to-member syntax: `connect(a, &A::signal, b, &B::slot)`.
- No singletons or global variables; dependencies are passed through constructors.
- User-visible strings in English, wrapped in `tr()`.
- Formatting: `.clang-format` (based on LLVM, indent 4, column limit 110).

**Comments — a reasonable amount, in a consistent format:**
- Every class gets a short `///` comment in its header (one or two sentences: what it is responsible for).
- Public methods get a `///` comment only when the name does not say everything: threading, ownership,
  error behavior, units, side effects.
- Inside functions, `//` comments explain *why* (intent, invariants, non-obvious decisions, protocol details),
  never restate *what* the code does.
- Full sentences, sentence case, no banner or separator comments, no commented-out code, no `TODO` without context.

```cpp
/// Accepts TCP connections and splits each client's byte stream into lines.
/// Lives in the intake thread; all signals are emitted from that thread.
class TcpServer : public QObject {
    ...
    /// Starts listening. Returns false and emits errorOccurred() if the port cannot be bound.
    bool start(const QHostAddress& address, quint16 port);
    ...
};

// A line may arrive split across several readyRead() calls, so keep the tail until '\n' shows up.
```

---

## 8. Build and Git

**Environment** (installed during preparation, see `docs/PROGRESS.md`): MSYS2 UCRT64 in `C:\msys64\ucrt64` with GCC,
CMake, Ninja and Qt 6 (`qt6-base`). `C:\msys64\ucrt64\bin` must be first in `PATH` for building and running.
In CLion: *Settings → Build, Execution, Deployment → Toolchains → + MinGW*, path `C:\msys64\ucrt64`, make it the default.

**CMake.** `find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Network Concurrent Widgets Test)`, `qt_standard_project_setup()`.
Targets: `lv_core` (static library), `LogVault` (`WIN32_EXECUTABLE`), `lv_tests` (registered with `add_test`).
C++20, warnings `-Wall -Wextra -Wpedantic`. `CMakePresets.json` with `debug` and `release` presets
(Ninja, `binaryDir` = `build/<preset>`), so the build is:

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

Stage 1 must include a test proving that the `QSQLITE` driver is available and `json_extract` works in it.

**Running:** from CLion, or from a shell with `C:\msys64\ucrt64\bin` in `PATH`. The demo runs on the author's laptop,
so no deployment packaging (`windeployqt`) is needed.

**Git.** Public GitHub repository `logvault`. `main` always builds; each stage is developed on a branch
`stage-N-<name>` and merged into `main` when done. Conventional Commits: `feat: …`, `fix: …`, `test: …`, `docs: …`,
`refactor: …`, `chore: …`.

---

## 9. Stages

One stage per session, in order. Status and handoff notes: `docs/PROGRESS.md`.

| # | Stage | Read | What to build | Done when |
|---|---|---|---|---|
| 0 | Preparation | — | Design, Qt installed, repository created | done (see `docs/PROGRESS.md`) |
| 1 | Skeleton | 7, 8 | CMake + presets (3 targets), placeholder main window, first test, `.clang-format`, build section in `README.md` | builds and runs; the test (incl. `QSQLITE` + `json_extract`) passes |
| 2 | Storage | 3.2, 4 | `Record`, `Level`, `Database`, `LogStore`: batch insert, sources, deletion | tests: create/open DB, 100,000 records in batches < 5 s, cascade delete, reading while writing |
| 3 | Intake | 3.3, 3.4, 5 | `TimeParser`, `LineParser`, `Ingestor`, `TcpServer`, `FileImporter`, `examples/python_client.py` | tests for parsing and TCP (split lines, several clients, dropped connection); 10,000 lines from Python → 10,000 records; a 100,000-line file imported |
| 4 | Search | 6 | `SearchQuery`, `SearchService`, `LogStore` queries (rows, counts, histogram, statistics) | tests for the search line (incl. injection attempts) and queries on a test DB; search on 1M records usually ≤ 1 s |
| 5 | Main window | 2, 3.3 | `MainWindow`, Sources/Receiver/Details panels, `FilterBar`, records table, import with progress, server start/stop, live mode | manual: open a DB, import, filters and search, live intake from Python |
| 6 | Analysis | 2.2, 6.4–6.7 | `HistogramWidget` (with zoom), `StatsView`, export, deleting old records | steps 2–7 of the demo (§11.1) work |
| 7 | Polish | 0.1, 2.3, 2.4 | `SettingsDialog`, themes, saved layout, recent databases, error messages, About dialog, icon, `README.md` with screenshots | the demo runs twice in a row without problems; §0.1 checked |

---

## 10. Tests (for verification only, not part of the submission)

Target `lv_tests`, Qt Test (`QTest`, `QSignalSpy`). Coverage:

- `TimeParser` — every format from §5.4, time zones, garbage input.
- `LineParser` — JSON (nested objects, different field names), plain text, broken lines, `msg_key`.
- `SearchQuery` — SQL and bound values for every example from §6.2; injection attempts via values and field names.
- `LogStore` — on a temporary database: insert, sources, search, histogram, statistics, deletion.
- `TcpServer` + `Ingestor` — through a `QTcpSocket` on a free port: whole lines, lines in pieces, several lines in one
  packet, several clients, a connection dropped mid-line, an oversized line.
- `FileImporter` — CRLF, BOM, stack traces, an empty file, cancellation.

---

## 11. Demo and defense

### 11.1. Demo scenario (≈ 7 minutes)

`examples/python_client.py` (not counted in the size):
- `LogVaultHandler(logging.Handler)` — sends records of Python's standard `logging` module to LogVault over TCP
  as JSON lines;
- `--demo` mode — simulates an online shop (services `gateway`, `auth`, `cart`, `payment`): a stream of requests
  at different levels, and after 20 seconds a burst of `Payment provider timeout after <N> ms` errors from the
  `bepaid` provider.

1. Create a database, start the receiver, turn on Live.
2. `python examples/python_client.py --demo` — a connection appears in the Receiver panel, records flow in,
   the histogram grows, and after 20 seconds there is a red spike.
3. Drag-select the spike on the histogram — the period narrows.
4. Keep only ERROR; Statistics → "similar messages": `Payment provider timeout after # ms` covers almost all errors.
5. Double-click the `provider` field in the record details → `provider:bepaid` in the search line; statistics by the
   `provider` field confirm that one provider is at fault.
6. Import an old log file — progress in the status bar; its records appear as a separate source.
7. Export the results to CSV and open them in Excel.

### 11.2. Talking points

- The path of a log line from `logging.error()` in Python to a row in the table (§3.4).
- Why the TCP byte stream has to be split into lines by the receiver, and what happens to partial lines.
- Which threads the program has and how they communicate (§3.3); why each has its own database connection.
- Why writes are batched, and what WAL is.
- How filters become SQL with bound parameters, and why that prevents injection.

---

## 12. Decisions log

| Question | Decision (2026-11-19) |
|---|---|
| Explanatory note and UML diagrams | Not now; revisit after the application is finished. |
| Demo machine | The author's own laptop; no deployment packaging needed. |
| Name | LogVault. |
| Repository | Public GitHub repository `logvault`. |
| Language | English for UI, code comments and all documents; the author is addressed in Russian in chat. |
| Process | One stage per session, in order; each session updates `docs/PROGRESS.md`. |
