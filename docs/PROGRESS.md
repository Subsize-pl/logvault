# Progress

One stage per session, in order (see `docs/DESIGN.md` §9). Update this file at the end of every session.

| # | Stage | Status | Date |
|---|---|---|---|
| 0 | Preparation | done | 2026-11-19 |
| 1 | Skeleton | done | 2026-11-19 |
| 2 | Storage | done | 2026-11-19 |
| 3 | Intake | done | 2026-11-20 |
| 4 | Search | **next** | |
| 5 | Main window | — | |
| 6 | Analysis | — | |
| 7 | Polish | — | |

## Environment

- MSYS2 UCRT64 (`C:\msys64\ucrt64`), updated 2026-11-19. Installed: GCC 16.2.0, CMake 4.5.0, Ninja 1.13.2,
  Qt 6.12.0 (`qt6-base`), SQLite 3.54.1.
- Verified with a throwaway CMake project: Qt compiles and runs, the `QSQLITE` driver is available,
  `json_extract` works.
- Python 3.14 comes with MSYS2 (`python` in `C:\msys64\ucrt64\bin`). `TestPythonClient` runs
  `examples/python_client.py` with the first Python 3 in `PATH` and is skipped if there is none.
- CLion must be switched to the MSYS2 toolchain by the author (one-time): *Settings → Build, Execution, Deployment →
  Toolchains → + MinGW*, path `C:\msys64\ucrt64`, make it the default. Agents build from the command line.

## Handoff notes

### Stage 0 — Preparation (2026-11-19)

- Design agreed: `docs/DESIGN.md` v0.4 (scope §1.2, stages §9, decisions §12).
- Repository created; `CLAUDE.md` describes how sessions work.
- Language: English for UI, comments and documents; talk to the author in Russian.
- Next — stage 1 (Skeleton): CMake with presets and three targets (`lv_core`, `LogVault`, `lv_tests`), a placeholder
  main window, a first Qt Test that also checks the `QSQLITE` driver and `json_extract`, `.clang-format`, and a build
  section in `README.md`.

### Stage 1 — Skeleton (2026-11-19)

**Built**

- Root `CMakeLists.txt`: C++20 (no extensions), Qt 6.5+ (`Core Sql Network Concurrent Widgets Test`),
  `qt_standard_project_setup()`, `enable_testing()`, and `lv_enable_warnings(target)` (`-Wall -Wextra -Wpedantic`,
  our targets only). `CMakePresets.json`: configure and build presets `debug` / `release` (Ninja, `build/<preset>`,
  compile commands), test preset `debug` (output on failure).
- Targets:
  - `lv_core` — static library in `src/core`; links Qt Core, Sql, Network, Concurrent (PUBLIC), never QtWidgets.
    `src/core` is its PUBLIC include directory. Holds `AppInfo` (`name()`, `version()`; the version comes from
    `project()` through the private define `LV_VERSION`).
  - `LogVault` — `src/gui`, `WIN32_EXECUTABLE`: `main.cpp` (application name and version from `AppInfo`, Fusion
    style) and `MainWindow` (title from `AppInfo`, centered "No database is open" label, 1024×640).
  - `lv_tests` — `tests`, one Qt Test executable, registered with `add_test`, environment `QT_QPA_PLATFORM=offscreen`.
- Executables land in the build root (`build/debug/LogVault.exe`, `build/debug/lv_tests.exe`):
  `qt_standard_project_setup()` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` on Windows.
- `.clang-format` (LLVM, indent 4, column limit 110, `PointerAlignment: Left`, `AccessModifierOffset: -4`);
  "Building" section in `README.md`.

**Tests**

- `tests/main.cpp` runs every test class through `QTest::qExec` (QTest options apply to each class) and exits with 1
  if any class failed. It first sets `QT_FORCE_STDERR_LOGGING=1`: without a console window (ctest pipes, IDE runners)
  Qt on Windows sends test output to the debugger only, and ctest would show nothing.
- Adding a test class: `tests/tst_<name>.h` with `class Test<Name> : public QObject` (`Q_OBJECT`, `private slots:`)
  and `tests/tst_<name>.cpp`; add both files to `tests/CMakeLists.txt`; in `tests/main.cpp` include the header and
  add `failed += runTests<Test<Name>>(argc, argv);`.
- `TestEnvironment` (`tst_environment`): `QSQLITE` is available; `json_extract` on `:memory:` returns 500;
  `PRAGMA journal_mode = WAL` returns `wal` on a file database in a `QTemporaryDir`. Each connection is removed by an
  RAII `ConnectionGuard` created before the handles, so it survives a failed check and closes the file before the
  temporary directory is deleted.

**Checks done**

- Clean rebuild from a deleted `build/debug`: no warnings, 1/1 tests passed (~0.2 s). The `release` preset also
  builds without warnings.
- `LogVault.exe` launched with `QT_FORCE_STDERR_LOGGING=1` and stderr redirected (a GUI app has no console, so this
  is how to catch Qt warnings): alive after 3 s with the "LogVault" window, no output, closed by `WM_CLOSE` with
  exit code 0.
- `grep -r "QtWidgets\|<QWidget\|<QMainWindow" src/core` finds nothing.

**ASSUMPTIONs**

- No C++ namespace for project classes (the DESIGN snippets use none); `AppInfo` is a namespace of free functions.
- Core headers are included by bare name (`#include "AppInfo.h"`) through the PUBLIC include directory.
- Test classes are named `Test<Name>` (PascalCase, §7.3) and live in `tests/tst_<name>.h/.cpp`.

**For the next stages**

- Call `lv_enable_warnings()` for every new target, and list headers in the target sources too (AUTOMOC).
- MSYS2 has no clang-format. Use CLion (it picks up `.clang-format`) or its bundled binary:
  `"C:\Program Files\JetBrains\CLion 2026.2.3\plugins\clion-radler\DotFiles\windows-x64\clang-format.exe" -i <files>`.
  The author chose `Type* name` and unindented `public:` / `private:` (DESIGN §7.3, §12), so `.clang-format` adds
  `PointerAlignment: Left` and `AccessModifierOffset: -4` to the LLVM base.

### Stage 2 — Storage (2026-11-19)

**Built** (all in `src/core`, no namespace; errors are reported as `bool` / `std::optional` + `errorString()`)

- `Record.h/.cpp` — `Level` and `Record` exactly as DESIGN §4.1, plus:
  - `QString levelName(Level)` — `"TRACE"` … `"FATAL"`, `"UNKNOWN"`.
  - `Level levelFromValue(int)` — stored value → `Level`; out of range → `Unknown`.
  - `QString messageKey(const QString&)` — runs of `0-9` → `#`, cut to `kMessageKeyMaxLength` (200) without
    splitting a surrogate pair. `LogStore` computes `msg_key` itself; stage 3 `LineParser` can reuse the function.
- `Database` (not copyable):
  - `bool open(const QString& path)` — opens or creates the file for the **calling** thread; succeeds once per
    object. `bool isOpen() const`, `QString path() const`, `QString errorString() const`,
    `QSqlDatabase connection() const` (asserts the calling thread), `static constexpr int kSchemaVersion = 1`.
  - Order inside `open()`: connection pragmas (`busy_timeout`, `foreign_keys`, `synchronous`) → read-only schema
    check → `journal_mode = WAL` (checked to return `wal`) → schema creation in `BEGIN IMMEDIATE` if the file has
    no tables. Rejected without touching the file: a newer `schema_version` ("… newer version of LogVault …"),
    tables without `meta` and non-SQLite files (SQLite code 26) ("… is not a LogVault database.").
  - Connection name `logvault-<object address>`; the destructor closes the connection, drops its own handle and
    then calls `removeDatabase`.
- `LogStore` (not copyable; holds a `QSqlDatabase` handle and three prepared queries):
  - `explicit LogStore(Database& database)`
  - `bool insertBatch(const QVector<Record>& records)` — one `BEGIN IMMEDIATE` transaction, prepared queries
    reused, sources via `INSERT OR IGNORE` + `SELECT id`; any failure → `ROLLBACK`, nothing written; an empty
    batch returns `true`.
  - `std::optional<QVector<SourceInfo>> sources()` — `SourceInfo{id, name, recordCount}`, sorted by name
    (case-insensitive).
  - `bool renameSource(qint64 id, const QString& name)` — trims; an empty, taken or missing name/source → error.
  - `std::optional<qint64> deleteSource(qint64 id, const std::atomic<bool>* cancel = nullptr)`
  - `std::optional<qint64> deleteRecordsOlderThan(qint64 ts, qint64 sourceId = 0, const std::atomic<bool>* cancel = nullptr)`
    — strictly older; chunks of `kDeleteChunkSize` (10,000), each its own transaction; the cancel flag is checked
    before each chunk (a cancelled `deleteSource` keeps the source row); returns the number of records deleted.
  - `std::optional<qint64> recordCount()`, `std::optional<QVector<Record>> latestRecords(int limit)` — minimal
    reads for the tests; stage 4 adds the real search queries here.
  - Error messages look like `Cannot rename the source: a source named "db" already exists`.

**Threading rules**

- One `Database` + `LogStore` pair per thread; create, use and destroy both in that thread. The `LogStore` must
  be destroyed before its `Database` (declare the `Database` first), or Qt reports the connection as still in use.
- To open another file, create a new `Database` object; a `LogStore` never sees its connection replaced.
- All write transactions are `BEGIN IMMEDIATE`: a second writer waits up to `busy_timeout` (5 s) for the lock
  instead of failing on a lock upgrade.

**Tests** (`tst_storage`, 21 test functions, a fresh `QTemporaryDir` per test)

- Record helpers; create and reopen; newer schema, foreign SQLite file (its journal mode stays `delete`), text
  file, missing directory and empty path rejected; a path with spaces and Cyrillic; 100,000 records; source
  counts; empty texts and an empty batch; a failed batch (test-only trigger) rolls back its records and new
  sources; rename and bad renames; `deleteSource` over 3 chunks plus the FK cascade; cancel; the strict
  `deleteRecordsOlderThan` boundary; Unicode round trip (Cyrillic, CJK, emoji); two writer threads × 30,000
  records while the main thread keeps reading counts.
- `tests/ConnectionGuard.h` is now shared by the test classes.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; ctest 1/1 passed (~2.4 s). The `release` preset builds without
  warnings.
- **Timing:** 100,000 records in batches of 500 — 0.9–1.3 s (debug build); the limit is 5 s.
- Concurrency test: about 47,000 successful reads while two writers wrote; no "database is locked".

**ASSUMPTIONs**

- `messageKey` replaces ASCII digits only.
- The name → id source cache lives for **one transaction**: between batches another connection may rename or
  delete a source, and a stale id would put records under the wrong source.
- `renameSource` trims names; `deleteRecordsOlderThan` keeps emptied sources; a null `QString` is stored as an
  empty text (the text columns are `NOT NULL`).
- `Database` holds the schema DDL and the pragmas (DESIGN §3.2 gives it that job); all other SQL is in `LogStore`.

**For the next stages**

- Stage 3: `Ingestor` (intake thread) and `FileImporter` (pool thread) each create their own `Database` +
  `LogStore` inside the thread they run in and call `insertBatch`; on `false` report `errorString()`.
- Stage 6: run `deleteSource` / `deleteRecordsOlderThan` in a pool thread with its own connection and pass the
  cancel flag.
- Keep test sources ASCII: write non-ASCII text as Unicode escapes in `u"..."` literals. Some agent tools turn
  such escapes into literal characters on write, so check the bytes of new files afterwards.

### Stage 3 — Intake (2026-11-20)

**Built** (all in `src/core`, no namespace for classes; `TimeParser` and `LineParser` are namespaces of free
functions, like `AppInfo`)

- `TimeParser` — `parsePrefix(text)` → `optional<Match{msecs, length}>` for a timestamp at the start of a text;
  `parse(text)` for a whole text (also Unix time: 10 digits = seconds, a fraction allowed; 13 digits = ms);
  `fromNumber(double)` for JSON numbers. Formats: `yyyy-MM-dd`, `T`/`t`/space, `HH:mm:ss`, optional `.`/`,`
  fraction (first three digits = ms), optional zone `Z`, `+03:00`, `+0300`, `+03`; no zone = local time.
  A fixed offset is plain arithmetic; local time goes through `QDateTime` with `QTimeZone::LocalTime`.
- `LineParser` — `ReceivedLine{QByteArray text (UTF-8), QString source, qint64 receivedAt, bool truncated}`;
  `Record parse(const ReceivedLine&, bool* ok = nullptr)` (never fails; `ok` = false for a `_parse_error`
  record); `Level levelFromName(QStringView)` (the synonyms of DESIGN 5.4). `Record::raw` = the line as received;
  `LogStore` still computes `msg_key`.
- `LineSplitter` (new, DESIGN 3.2) — splits a byte stream into lines for both intake paths: `'\n'` ends a line,
  a `'\r'` before it is dropped, blank lines are skipped, a line over `kMaxLineBytes` (64 KB) is cut at a UTF-8
  boundary and flagged, the rest of it skipped up to the next `'\n'`. `append(bytes)` → complete lines,
  `finish()` → the unfinished tail. Holds at most 64 KB + 1 bytes.
- `Ingestor` (QObject, intake thread) — `bool start(dbPath)` (opens its own `Database` + `LogStore`; must run in
  the thread the object lives in), `addLines(QVector<ReceivedLine>)`, `stop()` (writes pending lines, closes the
  database; the destructor calls it). Batch = 500 lines or 200 ms after the batch's first line; a bigger delivery
  is written in transactions of at most 500. `start()` on a running ingestor stops it first (pending lines go to
  the old database), so switching databases is one call.
- `TcpServer` (QObject, intake thread) — `bool start(QHostAddress, quint16 port)` (port 0 = a free port),
  `stop()`, `setDefaultSource(name)` (blank = `"tcp"`), `isListening()`, `stats()`. Up to 50 connections (the
  51st is closed at once, silently); one `LineSplitter` per client; a client that disconnects mid-line has its
  tail passed on; `stop()` passes on all tails. Constants `kMaxConnections`, `kStatsIntervalMs`.
- `FileImporter` (QObject, GUI thread; the work runs in `QThreadPool::globalInstance()` through QtConcurrent with
  its own connection) — `bool start(dbPath, QVector<ImportFile{path, source}>)` (false while running),
  `cancel()` (atomic flag, thread-safe), `isRunning()`, `waitForFinished()`; the destructor cancels and waits.
  256 KB blocks → `LineSplitter`; UTF-8 BOM skipped, UTF-16 rejected; indented lines join the previous record;
  transactions of 1,000 (`kBatchSize`); progress at most every 100 ms (`kProgressIntervalMs`).
- `examples/python_client.py` (stdlib only) — `LogVaultHandler(logging.Handler)` + `JsonLineFormatter`; CLI:
  `--count N`, `--demo` (until Ctrl+C or `--duration`), `--rate`, `--host`, `--port` (5170), `--file PATH`.

**Signals** (all connections across threads are queued automatically; no `qRegisterMetaType` needed in Qt 6)

| Signal | Emitted from | Meaning |
|---|---|---|
| `TcpServer::linesReceived(const QVector<ReceivedLine>&)` | intake thread | lines of one client → `Ingestor::addLines` (direct, same thread) |
| `TcpServer::statsChanged(const ReceiverStats&)` | intake thread | once per second while listening, and on every `start()`/`stop()` |
| `TcpServer::errorOccurred(const QString&)` | intake thread | `Cannot listen on 127.0.0.1:5170: The bound address is already in use`, `Cannot accept a connection: …` |
| `Ingestor::recordsWritten(int count)` | intake thread | a batch was committed (live mode in stage 5 listens to this) |
| `Ingestor::errorOccurred(const QString&)` | intake thread | the database did not open, or `Received lines were not saved. Cannot write N records: …` |
| `FileImporter::progressChanged(int percent, const QString& fileName)` | pool thread | percent of all bytes, current file |
| `FileImporter::finished(const ImportSummary&)` | importer's thread (GUI) | `added`, `unparsed`, `elapsedMs`, `cancelled`, `error` |

**Stats structure** — `ReceiverStats{bool listening; QString address; quint16 port; QString error;
QVector<ConnectionInfo> connections; qint64 linesPerSecond; qint64 totalLines}`,
`ConnectionInfo{quint64 id; QString address /* "ip:port" */; qint64 connectedAt /* ms UTC */; qint64 lines}`.
Panel state: `listening` → running; not listening and `error` not empty → error (show `error`); else stopped.
`totalLines` counts from the last `start()`; `connections` are in connection order.

**How to wire the intake thread** (stage 5 copies this; `tests/IntakeRig.h` does exactly this)

```cpp
// Create (GUI thread). The two objects have no parent: they move to another thread.
m_intakeThread = new QThread(this);
auto* ingestor = new Ingestor;
auto* server = new TcpServer;
ingestor->moveToThread(m_intakeThread);
server->moveToThread(m_intakeThread);
connect(m_intakeThread, &QThread::finished, ingestor, &QObject::deleteLater); // deleted in their own thread
connect(m_intakeThread, &QThread::finished, server, &QObject::deleteLater);
connect(server, &TcpServer::linesReceived, ingestor, &Ingestor::addLines);   // same thread: direct call
connect(ingestor, &Ingestor::recordsWritten, this, ...);                      // queued to the GUI thread
connect(ingestor, &Ingestor::errorOccurred, this, ...);
connect(server, &TcpServer::errorOccurred, this, ...);
connect(server, &TcpServer::statsChanged, receiverPanel, ...);
m_intakeThread->start();

// Open a database / start the receiver: queued calls, results come back as signals.
QMetaObject::invokeMethod(ingestor, [=] { ingestor->start(dbPath); });
QMetaObject::invokeMethod(server, [=] { server->setDefaultSource(source); server->start(address, port); });
// Receiver → Stop:
QMetaObject::invokeMethod(server, [=] { server->stop(); });
// Another database: one queued call; pending lines go to the old file first.
QMetaObject::invokeMethod(ingestor, [=] { ingestor->start(newPath); });

// Shutdown (DESIGN 3.3 order): server leftovers → ingestor writes them and closes the DB → thread ends.
QMetaObject::invokeMethod(server, [=] { server->stop(); ingestor->stop(); }, Qt::BlockingQueuedConnection);
m_intakeThread->quit();
m_intakeThread->wait(); // both objects are deleted in the intake thread before wait() returns
```

- Use `Qt::BlockingQueuedConnection` only while the thread runs (otherwise it deadlocks) and never from the
  intake thread itself. Do not call methods of `Ingestor`/`TcpServer` directly from the GUI thread.
- Before the intake thread stops, cancel and wait for the importer (its destructor does the same):
  `m_importer.cancel(); m_importer.waitForFinished();`.

**Tests** (six new classes; QTest totals in brackets; the whole suite has 200)

- `TestTimeParser` (58, with data rows): all formats, offsets, fractions, Unix time, garbage, and 2,000 random
  instants/offsets checked against `QDateTime`.
- `TestLineParser` (58): JSON names/priority/case, nesting, arrays, unusable values kept in fields, long ids,
  plain text variants, unrecognized lines, truncated flag, invalid UTF-8, Unicode.
- `TestLineSplitter` (13): line breaks, pieces, byte-by-byte, blank lines, exact 64 KB limit, UTF-8 cut, skipping.
- `TestIntake` (18): Ingestor batches/delay/stop/errors in the test thread; TcpServer + Ingestor in a real QThread
  (`IntakeRig`) with client `QTcpSocket`s: whole lines, split lines, 2,000 lines in one write, 5 interleaved
  clients, dropped mid-line, 200 KB line, 51st connection, busy port, leftovers at stop, stats.
- `TestFileImporter` (14): CRLF + BOM, stack traces, empty files, unparsed count, sources, limits, missing file,
  UTF-16, database failure, double start, cancel (whole transactions only), 100,000 lines + progress throttling.
- `TestPythonClient` (4; skipped without Python 3 in PATH): `--count 10000` over TCP → exactly 10,000 records in
  order; a `--demo --file` minute imports with 0 unparsed lines.
- `TestIntake`, `TestFileImporter` and `TestPythonClient` call `QTest::failOnWarning()` in `init()`: any Qt
  warning (a thread destroyed while running, a connection still in use) fails the test.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; ctest 1/1 passed (~13 s). The `release` preset builds without
  warnings. Every commit of the branch builds and passes the tests (`git rebase -x`).
- **Timings:** 100,000-line file (7.4 MB, JSON and plain lines) — 2.2 s in the debug build (45,000 lines/s),
  1.3–1.5 s in release (65,000–78,000 lines/s); DESIGN 1.4 asks for 20,000 lines/s. 10,000 lines from
  `python_client.py --count` → 10,000 records in ~1.2 s (the Python client is the bottleneck at ~12,000 lines/s).
- Python client by hand: a server that closes the connection → the next records arrive over a new connection;
  no server → one line on stderr per record, no exception.

**ASSUMPTIONs**

- Time: seconds are required; the fraction keeps milliseconds (the rest is dropped); `T`/`Z` may be lower-case;
  Unix time also as a JSON string.
- JSON: a special field is taken only when its value can be used — a time that does not parse, an unknown or
  numeric level, an object as message stay among the fields. A JSON line without a usable message shows the
  whole line as its message.
- Plain text: recognized when it has a timestamp or a level (else `_parse_error`); the message is trimmed,
  `raw` is the line as received.
- Blank lines are skipped; the 64 KB limit counts bytes without the line break.
- A batch that fails is rolled back, reported and dropped (no retries).
- Import: a joined record is limited to 64 KB (more indented lines are dropped, the record is flagged
  `_truncated`); the first file that cannot be opened or read stops the import, lines read before it are written;
  a UTF-16 file is rejected; a line without a timestamp gets the time it was read; a blank source name becomes the
  file name without its last extension.
- Python client: a custom `TRACE` level (5); `--count` lines carry `"service":"loadtest"`.

**DESIGN changes** — `LineSplitter` added (3.2, 7.1, 7.2); 5.1: blank lines are skipped; 5.2: one
`statsChanged(ReceiverStats)` instead of `clientConnected`/`clientDisconnected` and loose stats arguments; 5.3:
block reading through `LineSplitter`, the joined-record limit, UTF-16 and unreadable files; the `LineParser` row
no longer claims `msg_key`.

**For the next stages**

- Stage 4 (search) does not touch intake. Records have `msg_key` and flattened `fields` (`http.status`), so
  `json_extract(r.fields, '$."http.status"')` works on data from both intake paths.
- Stage 5: the wiring above; the Receiver panel shows `ReceiverStats`; the import dialog builds `ImportFile`s
  (default source = `QFileInfo::completeBaseName()`, the importer applies it too when the name is blank) and shows
  `ImportSummary`; live mode listens to `recordsWritten`.
- Try the client: `python examples/python_client.py --demo --file shop.log` writes ten minutes of shop logs for
  an import; `--demo` and `--count N` need the running receiver (stage 5); `python examples/python_client.py -h`.
- Shared test helpers: `tests/IntakeRig.h` (intake thread wiring), `tests/StoredRecords.h` (all records by id).
- The tool that writes files turned `\uXXXX` escapes into characters again in this stage; after writing a test,
  check that it is ASCII:
  `python -c "import glob; print([f for f in glob.glob('tests/*') if any(b > 127 for b in open(f,'rb').read())])"`.
