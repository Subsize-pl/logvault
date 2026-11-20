# Progress

One stage per session, in order (see `docs/DESIGN.md` §9). Update this file at the end of every session.

| # | Stage | Status | Date |
|---|---|---|---|
| 0 | Preparation | done | 2026-11-19 |
| 1 | Skeleton | done | 2026-11-19 |
| 2 | Storage | done | 2026-11-19 |
| 3 | Intake | done | 2026-11-20 |
| 4 | Search | done | 2026-11-20 |
| 5 | Main window | done | 2026-11-20 |
| 6 | Analysis | done | 2026-11-20 |
| 7 | Polish | done | 2026-11-20 |
| 8 | Final review | done | 2026-11-20 |

All stages are done (version 1.0.0). How to run the demo: the stage 7 notes below; the build and the Python client:
`README.md`. Stage 8 (a review of the finished application) is at the end.

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

### Stage 4 — Search (2026-11-20)

**Built** (all in `src/core`; errors as `std::optional` / `bool` + `errorString()`, like stage 2)

- `SearchQuery.h/.cpp` — the values of a search and the condition builder:
  - `StatsMode`, `SearchRequest` (DESIGN 6.1; `levels` defaults to all seven, an empty list matches nothing; `from`
    inclusive, `to` exclusive, 0 = unbounded), `SqlFragment{QString sql; QVariantList values}` (a condition over
    `records r` and its values in placeholder order), `LevelCounts` (`std::array<qint64, kLevelCount>`, index = `Level`
    value; `kLevelCount = 7` is in `Record.h`), `Histogram{start, step, bars}` with `static forRange(first, last)` (the
    step table of DESIGN 6.4, times inclusive) and `total()`, `StatsRow{value, count, id, noField}`.
  - `namespace SearchQuery`: `build(request)`; `isValidFieldName(name)` (`[A-Za-z0-9_.@-]+`); `fieldValue(name)` (the
    value expression, binds the path `$."name"` twice); `fieldMention(name)` (`instr(r.fields, ?) > 0` with `"name"`);
    `quote(text)` (a part that reads back exactly: quotes, `\"` and `\\`).
- `LogStore` — `beginRead()` / `endRead()` (one read transaction), `findRecords(condition, limit)` (newest first;
  replaces `latestRecords`, which is gone: `findRecords({}, n)` gives the newest n), `matchSpan(condition)` →
  `MatchSpan{found, firstTs, lastTs}`, `histogram(condition, first, last)`, `statistics(condition, mode)` (level, source,
  message; other modes give no rows), `fieldStatistics(condition, field, matchCount)`, `kStatsRowLimit = 100`.
- `SearchService.h/.cpp` — `SearchResult{generation, rows, total, histogram, stats, elapsedMs, error}` and
  `SearchService` (QObject, GUI thread only):
  - `setDatabasePath(path)`, `databasePath()`, `quint64 search(request)` (generations grow from 1),
    `waitForFinished()`, signal `finished(const SearchResult&)`; the destructor waits for running searches.
  - Runs in its own `QThreadPool` of 2 threads. A job opens its own `Database` + `LogStore` in the pool thread and runs,
    in one read transaction: span (`min`/`max`) → histogram (total = sum of the bars) → rows between the oldest and
    the newest match → statistics, only when `statsMode != None`.
  - A result is emitted only if no newer `search()` or `setDatabasePath()` came after its start; a stale job stops
    between its queries. Errors arrive as results with `error` set and everything else empty: `No database is open.`,
    `Cannot search C:\...: the file does not exist.` (a search never creates a file), `... is not a LogVault database.`,
    `Cannot compute statistics: "a b" is not a valid field name.`, SQLite errors.
  - `SearchResult`, `Histogram`, `StatsRow` are registered as meta types in the constructor.

**Search line** (DESIGN 6.2) — what stages 5 and 6 build into it

| Action | Part to add |
|---|---|
| Search for this value (details panel, statistics by field) | `name + ':' + SearchQuery::quote(value)` |
| Exclude this value | `'-' + name + ':' + SearchQuery::quote(value)` |
| Double-click on the "(no field)" row (`StatsRow::noField`) | `'-' + name + ":*"` |
| Double-click on a similar message | `SearchQuery::quote(longest #-free part)` |

Quoted text is literal (no field syntax, no `*` wildcard). Field names are case-sensitive; values are compared without
case (Latin letters only, as SQLite does). JSON `true`/`false` match as words, JSON `null` counts as no field.

**How to wire it** (stage 5)

```cpp
m_search = new SearchService(this);
connect(m_search, &SearchService::finished, this, &MainWindow::showSearchResult);
m_search->setDatabasePath(path);            // after opening a database; then start a search
m_runningSearch = m_search->search(request); // keep the number for live mode

void MainWindow::showSearchResult(const SearchResult& result) {
    if (result.generation == m_runningSearch) m_runningSearch = 0;
    if (!result.error.isEmpty()) { /* status bar or message box */ return; }
    // result.rows -> table, result.histogram -> histogram, result.stats -> Statistics tab,
    // "Found result.total of <all records> · result.elapsedMs ms"
}
```

- Live mode (DESIGN 6.6, "never while the previous search is still running"): a search is running while
  `m_runningSearch != 0`. After `setDatabasePath()` the old result never arrives, so reset the number there.
- Relative periods: set both `from` and `to` (`to` = now + 1), so the bars span the whole period, also when it is empty.
- `rows` are `Record`s with the source name, newest first; `maxRows` comes from the settings (default 10,000).

**Tests** (three new classes; the whole suite: 387 passed, 1 skipped — the performance test)

- `TestSearchQuery` (92 with data rows): every example of DESIGN 6.2, field forms, exclusions, quotes and escapes,
  unbalanced quotes, empty input, LIKE escaping, Cyrillic, injection attempts in values and in field names, the level,
  source and period filters, their combination, name validation, `quote()` round trips. Each case also checks that
  the number of `?` equals the number of values.
- `TestSearch` (85 + perf): a fixture of 17 known records — order with equal times, limits, the search line and the
  filters on real data (period bounds, case, booleans, null, numbers vs strings, `%` and `_`), injection attempts
  leave the database unchanged, a 3,000-word line gives no matches or a clean error, histogram bars, the step table (1 s … 60 days) and
  5,000 random ranges, every statistics mode with limits and "(no field)", the `instr` hint never hiding a record, the
  read snapshot while another connection writes.
- `TestSearchService` (10): async result and growing generations, statistics only when asked, five searches in a row
  deliver only the last, a database switch drops the old result, error results (no file is created), empty bars of an
  empty bounded period, consistent results while another thread writes 20,000 records, destruction during a search.
  `failOnWarning()` in `init()`.

**Performance** — `LV_PERF=1` runs `TestSearch::searchesMillionRecordsQuickly` (about a minute; it writes a
file of a few hundred MB in a temporary directory):

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake --build --preset debug && LV_PERF=1 QT_QPA_PLATFORM=offscreen ./build/debug/lv_tests.exe | grep perf:
```

Debug build, 1,000,000 generated shop records, warm cache, two runs (the test passes if typical searches take ≤ 1 s
and statistics over all records ≤ 1.5 s):

| Search | Time | Matches |
|---|---|---|
| all records (default view) | 0.30 s | 1,000,000 |
| word `timeout` | 0.54–0.70 s | 17,444 |
| field `provider:bepaid` | 0.56–0.63 s | 92,528 |
| levels ERROR+FATAL, last 24 hours | 0.08 s | 10,770 |
| `timeout provider:bepaid -env:test` | 0.64–0.65 s | 13,610 |
| statistics by similar messages, ERROR only | 0.22 s | 69,825 |
| statistics by similar messages, all records | 0.98–1.02 s | 1,000,000 |
| statistics by field `provider`, all records | 0.94–0.95 s | 1,000,000 |
| writing the 1,000,000 records (batches of 10,000) | 12.5–14.5 s | |

The first version (count, rows, histogram as in the old DESIGN 6.3) took 1.46 s for a field search and 1.58 s for
field statistics; the query shape of DESIGN 6.3 fixed that. Statistics by message over all records stay at about 1 s:
the sort of 1,000,000 `msg_key` values takes ~0.65 s. An index on `msg_key` brings that query to ~0.06 s (measured), but
it changes the schema and needs `GROUP BY +r.msg_key` whenever the condition is not empty (else SQLite walks that
index and takes 1.5 s for a text search) — not done; the author decides.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; ctest 1/1 passed. Every commit of the branch builds and passes the tests
  (`git rebase -x`). The performance test passes (numbers above).
- `grep` over the diff: no values concatenated into SQL (all through `?`, the JSON path and the `instr` needle
  included), no debug output besides the `qInfo("perf: ...")` lines of the opt-in test, sources ASCII.

**ASSUMPTIONs**

- Search line: inside quotes `\"` and `\\` stand for `"` and `\`; quoted text is literal; an unclosed quote runs to the
  end. `name:` alone, a lone `-` and a part with an invalid name are text; only one leading `-` excludes; `*` is a
  wildcard in unquoted field values only; field names are case-sensitive (JSON keys are).
- JSON `true`/`false` compare and group as the words `true`/`false` (SQLite's `json_extract` gives 1/0); JSON `null`
  counts as no field.
- Field statistics keep exact values as groups (`BePaid` and `bepaid` are two groups), although the search ignores case.
- Histogram bars are aligned to multiples of the step since 1970-01-01 UTC (UTC midnight for days, Thursday for weeks);
  a range over 80 × 30 days gets a multiple of 30 days. A bounded period without matches gets empty bars; an unbounded
  one gets none.
- `SearchRequest` enables all levels by default. A search of a missing file is an error (no file is created).
  `setDatabasePath()` drops results of earlier searches. `elapsedMs` counts from `search()` to the end of the job.
- The `instr` hint relies on `fields` being written by LogVault (`QJsonDocument`, compact): a record with a field always
  contains `"name"`. A database edited by hand with escaped key characters would hide such records from field searches.

**DESIGN changes** — 3.3 (the search pool, stale searches stop, shutdown waits for it), 6.1 (`Histogram`, `StatsRow`,
`SqlFragment`, request defaults, failed and dropped results), 6.2 (quotes, escapes, the value expression, the `instr`
hint, the UI filters), 6.3 (new query shape and why), 6.4 (alignment, long ranges, empty periods), 6.5
(`StatsRow::id`, double-click on "(no field)").

**For the next stages**

- Stage 5: the wiring above. The GUI thread needs its own `Database` + `LogStore` for small reads (`sources()`, the
  total record count for the status bar) — never share the search or intake connections.
- Stage 6: `HistogramWidget` draws `bars[i][level]`; bar i covers `[start + i*step, start + (i+1)*step)`, so a drag
  over bars a..b gives the period `[start + a*step, start + (b+1)*step)`. `StatsView` shows `value`, `count` and
  `count / total`; `id` and `noField` drive the double-click. The `Exporter` must stream all matches, not `maxRows`:
  add a streaming read to `LogStore` (for example, chunks ordered by `r.ts DESC, r.id DESC`) that takes the same
  `SqlFragment`.

### Stage 5 — Main window (2026-11-20)

**Built**

- Core additions (stage 5 needed them; DESIGN updated):
  - `SearchResult::sources` — every source with its record count, read in the search's own read transaction. The
    Sources panel and "Found X of Y" come from it, so the GUI thread never opens a connection (the stage 5 rule "all DB
    work off the GUI thread" wins over the stage 4 hint of a GUI-thread `LogStore`).
  - `DatabaseJobs` (QObject, GUI thread; jobs in `QThreadPool::globalInstance()` with their own connection; one job at
    a time): `open(path)` (creates a new file's schema, checks an old one), `renameSource(path, id, name)`,
    `deleteSource(path, id)` (cancel between chunks), `cancel()`, `isRunning()`, `waitForFinished()`; signal
    `finished(DatabaseJobResult{kind, databasePath, deleted, cancelled, error})`.
  - `LineParser::scalarText(QJsonValue)` is public: the details panel shows field values the way the search sees them.
- Build: `src/gui` is the static library `lv_gui` (all but `main.cpp`); `LogVault` is `main.cpp` + `lv_gui`; `lv_tests`
  links `lv_gui` and creates a `QApplication` (offscreen under ctest).
- `src/gui`: `MainWindow`, `FilterBar`, `PeriodDialog`, `SourcesPanel` (a two-column `QTreeWidget`: name, count),
  `ReceiverPanel`, `RecordDetailsPanel`, `RecordTableModel`, `ImportDialog`, `LiveUpdater` (no widgets), `Theme` (level
  and state colors, the monospace font — Consolas at the UI size), `Format` (`count` "12,345" in English whatever the
  locale, `timestamp`, `dateTime`, `duration`, `firstLine`). About 2,500 lines with headers.

**Window structure**

```
MainWindow
├── menus   File: New Database (Ctrl+N), Open Database (Ctrl+O), Import Files (Ctrl+I), Exit
│           Data: Rename Source, Delete Source        Receiver: Start/Stop Receiver (Ctrl+R)
│           View: Panels ▸ (Sources, Receiver, Record Details), Live Mode (Ctrl+L), Refresh (F5); Ctrl+F = search line
├── toolbar Open, Import | Start/Stop Receiver | Live, Refresh
├── docks   Sources (left, top), Receiver (left, bottom), Record Details (right)
├── center  QStackedWidget: placeholder page (New / Open buttons) | workspace page:
│             FilterBar
│             QSplitter: histogram area (a QFrame, 110 px) | QTabWidget: "Records (N)" QTableView, "Statistics" (empty)
└── status  search status (stretch) | job label + Cancel | import label + progress + Cancel | live indicator
```

- **Histogram placeholder and Statistics tab** are made in `MainWindow::createWorkspace()`: the `QFrame` named in a
  comment there and `m_tabs->addTab(new QWidget, tr("Statistics"))`. Stage 6 replaces both in that function.
- Defaults are constants at the top of `MainWindow.cpp`: `kReceiverAddress` (127.0.0.1), `kReceiverPort` (5170),
  `kMaxRows` (10,000) — stage 7 turns them into settings.

**Signals between the parts**

| From | Signal | To |
|---|---|---|
| `FilterBar` | `searchRequested()` (levels, period, Enter, Search, cleared line) | `MainWindow::search()` |
| `SourcesPanel` | `selectionChanged()` (only when the selected ids change) | `updateActions()` + `search()` |
| `RecordDetailsPanel` | `searchTermRequested(term)` | `FilterBar::addSearchTerm()` (appends, searches) |
| `RecordDetailsPanel` | `levelFilterRequested(level, exclude)` | `FilterBar::showOnlyLevel()` / `hideLevel()` |
| `RecordDetailsPanel` | `sourceFilterRequested(name, exclude)` | `SourcesPanel::showOnlySource()` / `hideSource()` |
| table selection, scroll bar, model reset | — | details panel, `LiveUpdater::setPaused()` |
| `SearchService` | `finished(SearchResult)` | `showSearchResult()`: table, sources, tab title, status |
| `DatabaseJobs` | `finished(DatabaseJobResult)` | `showJobResult()`: switch database, refresh, messages |
| `FileImporter` | `progressChanged()` / `finished()` | status bar / summary box + search |
| `Ingestor` | `recordsWritten()` | `LiveUpdater::recordsWritten()` → `refreshDue()` → `search()` |
| `Ingestor`, `TcpServer` | `errorOccurred()` | `reportError()`: one non-modal box, repeats update it |
| `TcpServer` | `statsChanged()` | `ReceiverPanel::setStats()`, the receiver action's text and icon |

**Behavior**

- Opening: `DatabaseJobs::open()` checks (or creates) the file in the background; the current database stays open
  until then. On success `closeDatabase()` runs, a new intake thread is created exactly as in the stage 3 notes,
  `SearchService::setDatabasePath()`, and a search starts. The same file again does nothing.
- `closeDatabase()` (window close, switching): blocking `server->stop(); ingestor->stop()` → cancel the importer and
  the jobs → `SearchService::setDatabasePath({})` (running searches go stale) → wait for the importer, the jobs and the
  search pool → `quit()` + `wait()` the intake thread. A wait cursor shows meanwhile.
- One database job at a time: New, Open, Rename and Delete are disabled while one runs; the status bar shows it
  (a deletion has Cancel).
- Search status: "Found X of Y · showing the newest N · 0.18 s" (Y = sum of the source counts); "Searching..." only
  after 300 ms. Relative periods send `to = now + 1`.
- "Search for this value": a field gives `name:` + `SearchQuery::quote(value)`, the message its quoted first line
  (at most 200 characters), the level and the source change the buttons and the selection; the time and null fields
  can only be copied. Excluding a source when "All" is selected selects every other source.
- Live mode: `LiveUpdater` (1 s, never during a search); paused while the table is scrolled down or a record is
  selected; the indicator says "Live paused · Resume" (the link clears the selection and scrolls up; Esc in the table
  clears the selection).
- Import: menu or drag and drop (the dialog opens after the drop returns, so Explorer is not blocked); `ImportDialog`
  has an editable combo per file (file name without extension, existing sources); progress + Cancel in the status bar;
  summary box "Added N records, M lines not parsed, took T"; a search afterwards. Lines that name their own source
  (JSON `service`) keep it.
- Ctrl+C copies the raw lines of the selected rows; the header's context menu hides and shows columns (the last
  visible one stays); columns can be moved and resized.

**Tests** (five new classes and one new function; the whole suite: 432 passed, 1 skipped — the performance test)

- `TestDatabaseJobs` (7): open/create, a text file rejected, rename, taken name, delete over two chunks, cancel keeps
  the source and the rest, one job at a time. `failOnWarning()` in `init()`.
- `TestRecordTableModel` (8, with `QAbstractItemModelTester`): headers, texts, first line, cutting between surrogates,
  colors and fonts, raw lines in row order, resets.
- `TestLiveUpdater` (6, 100 ms interval): quiet while off, catch-up refresh, at most once per interval, waits for the
  running search, waits while paused, state changes.
- `TestFilterBar` (7): defaults, toggling, Ctrl+click, relative and custom periods, search terms, history of 20.
- `TestPanels` (6): "All" and the counts, show only / hide a source, selection across refreshes, "All" vs sources,
  field values and the filters the details panel asks for.
- `TestSearchService::deliversSourcesWithEveryResult`.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; ctest 1/1 passed. Every commit of the branch builds and passes the
  tests (`git rebase -x`). `grep`: no SQL in `src/gui`, no debug output, sources ASCII.
- Smoke run: `LogVault.exe` alive after 5 s with the title "LogVault", no output on stderr, closed by `WM_CLOSE`.
- A scripted run of the real `MainWindow` on the offscreen platform (a throwaway program in the session scratchpad,
  not committed) passed with no Qt warnings: create a database; import the 2,000-line sample through the file, source
  and summary dialogs; Ctrl+click ERROR and back; phrase and `seq:19*` searches; clearing the line; start the receiver;
  live mode shows 300 TCP lines; a selection pauses live mode and Resume catches up; double-click on `provider` gives
  `provider:"bepaid"`; a busy port gives "Cannot listen on 127.0.0.1:5170: The bound address is already in use" + the
  hint; rename and delete a source; close while `python_client.py --count` keeps sending; reopen — all 7,156 records
  are there. Nothing was checked visually (no screen).

**ASSUMPTIONs**

- A running receiver keeps running when another database is opened.
- New Database never replaces a file: an existing name gets a message (use Open Database).
- One database job at a time; a database switch cancels a running import (committed transactions stay; the summary
  says it was cancelled).
- The table shows local time with the date (`yyyy-MM-dd HH:mm:ss.zzz`); counts use English separators.
- The search history lives for the session only (20 entries).

**DESIGN changes** — 2.2 (Sources is a two-column `QTreeWidget`), 3.1 and 8 (`lv_gui`), 3.2 (`DatabaseJobs`,
`ImportDialog`, `LiveUpdater`, `Format`), 3.3 (`DatabaseJobs` in the pool; the shutdown order also when switching
databases), 6.1 (`SearchResult::sources`), 7.1 (files), 11.1 (`provider:"bepaid"`).

**Manual checklist for the author** — not run by the author yet (the scripted run above covers the same steps
without a screen):

1. Build and start: `cmake --preset debug && cmake --build --preset debug`, then `build\debug\LogVault.exe`
   (with `C:\msys64\ucrt64\bin` in `PATH`) or run `LogVault` from CLion.
2. File → New Database… → `demo.lvdb`: the title becomes "LogVault — demo.lvdb", the status says "Found 0 of 0".
3. `python examples/python_client.py --file sample.log --count 2000`, then File → Import Files… (or drag the file onto
   the window): the source dialog, progress in the status bar, the summary "Added 2,000 records…".
4. Filters: level buttons (click, Ctrl+click), Period presets and Custom range…, the search line (`"line 1999 "`,
   `seq:19*`, `-seq:1*`, Enter / Search, the history drop-down while typing), sources (Ctrl+click several), the record
   details (right-click a field → Search for / Exclude / Copy; double-click), Ctrl+C on rows, the header menu.
5. Receiver → Start Receiver (Ctrl+R): the Receiver panel says "Listening on 127.0.0.1:5170".
6. Live Mode on (Ctrl+L), `python examples/python_client.py --demo`: a connection in the Receiver panel, lines/s,
   new records about once per second; select a record → "Live paused · Resume".
7. Close the window while the demo is sending, start LogVault again, File → Open Database… → `demo.lvdb`: the records
   are there; no error messages.

**For the next stages**

- Stage 6: put `HistogramWidget` in place of the `QFrame` in `MainWindow::createWorkspace()`; a drag-select calls
  `FilterBar::setCustomPeriod(from, to)` (it emits `searchRequested()`); Esc needs the previous period, so `FilterBar`
  must remember the period before a custom one. `StatsView` replaces the empty Statistics tab; set
  `request.statsMode` in `MainWindow::search()` only while that tab is current and search again when the tab changes;
  double-clicks use `FilterBar::showOnlyLevel()`, `SourcesPanel::showOnlySource()` and `FilterBar::addSearchTerm()`.
  Deleting old records fits `DatabaseJobs` (a new kind with the same cancel flag) + a Data menu action; `Exporter`
  needs a status bar indicator like the import one.
- Stage 7: the constants at the top of `MainWindow.cpp` become settings; docks and the toolbar need object names for
  `saveState()`; the recent list goes on the placeholder (`createPlaceholder()`).

### Stage 6 — Analysis (2026-11-20)

**Built**

- Core:
  - `LogStore::visitRecords(condition, visit)` — every match, newest first, one row at a time (forward-only query);
    `countRecords(condition)`; the chunked deletions take an optional `DeleteProgress(deleted, total)` callback (they
    count first, then report before the first chunk and after each one).
  - `DatabaseJobs::deleteRecordsOlderThan(path, before, sourceId)` — kind `DeleteOldRecords`, same cancel flag;
    signal `progressChanged(int percent)` (pool thread) for both deletions.
  - `Exporter` (QObject, GUI thread; the job in `QThreadPool::globalInstance()` with its own connection):
    `start(dbPath, request, path, ExportFormat::Csv|JsonLines, separator)`, `cancel()`, `isRunning()`,
    `waitForFinished()`, static `listSeparator()` (`HKCU\Control Panel\International\sList`, `,` fallback); signals
    `progressChanged(percent)` (pool thread, at most every 100 ms) and `finished(ExportSummary{path, exported,
    elapsedMs, cancelled, error})`. Count and rows in one read transaction; written through `QSaveFile`.
- GUI:
  - `HistogramWidget` (QPainter) — stacked bars (FATAL at the bottom), scale label, round-time axis labels, hover,
    rich-text tooltip, drag-select of whole bars → `FilterBar::zoomToPeriod()`; Esc → `FilterBar::restorePreviousPeriod()`
    (a stack of up to 50 periods; choosing a period in the combo clears it). `setCustomPeriod()` is gone.
  - `StatsView` — Group by Level / Source / Similar messages / Field… (+ field name, Enter applies), Value / Count /
    Share table, summary label; double-click or Enter per DESIGN 6.5 (`SourcesPanel::showOnlySourceId()` is new).
    `MainWindow::search()` asks for statistics only while the tab is current; switching to it searches.
  - File → Export Results… (Ctrl+E, toolbar), Data → Delete Records Older Than… (`DeleteRecordsDialog` + confirmation).
  - `TaskStatus` — one status bar widget per background task (job, import, export): text, progress, Cancel. The stage 5
    labels/buttons were replaced by it.
  - Appearance fixes the author asked for after checking stage 5 in the dark Windows theme: the histogram starts at
    120 px (`QSplitter::setSizes`; before, the splitter gave the placeholder 480 px); `Theme::neutralPalette()` (slate
    `Highlight`/`AlternateBase` instead of the purple accent, gray placeholder text — the dark palette made it white),
    applied in `main.cpp` and again on `colorSchemeChanged`; level colors as text go through
    `Theme::levelTextColor()` (contrast ≥ 4.5:1, same hue) in the table, record details (now bold), statistics and level
    buttons; `Theme::glyphIcon()` draws Import, Export, Start, Stop and Live (Live had no icon) in palette colors.
    Real dark palette read on this machine: Base `#2d2d2d`, AlternateBase was `#400e59`, Highlight `#a94dc1`.

**Decision (author, 2026-11-20):** no index on `msg_key` (see the stage 4 timings); statistics by message over all of
1,000,000 records stay at about 1 s.

**Tests** (the whole suite: 472 passed, 1 skipped — the performance test)

- New classes: `TestExporter` (7: CSV quoting of commas, quotes, CR/LF, the BOM, the separator parameter, JSON Lines
  that parse back with Unicode and control characters, every match in order past the block size, cancel leaves no file
  and keeps an old one, errors create no files, the list separator), `TestTheme` (4: contrast on light and dark
  backgrounds, the neutral palette, glyph icons), `TestHistogram` (8 + data rows: selection → period, label interval
  and boundaries, label text, tooltip, drag, click and Esc).
- New functions: `TestSearch::visitsEveryMatchNewestFirst`, `countsMatches`; `TestStorage::deletionReportsProgress`;
  `TestDatabaseJobs::deletesOldRecordsWithProgress`, `cancelKeepsOldRecordsNotReached`;
  `TestFilterBar::zoomsBackThroughEarlierPeriods`, `choosingAPeriodForgetsZooms`.
- By the author's choice (time), `StatsView`, `TaskStatus`, `DeleteRecordsDialog` and the export/deletion wiring got no
  unit tests; the scripted run below covers them.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; ctest passed. The `release` preset builds without warnings. Every commit
  of the branch builds without warnings and passes the tests (checked out one by one; `git rebase -x` failed to
  reconfigure CMake inside git's environment here).
- Smoke run: `LogVault.exe` alive after 4 s with the title "LogVault", nothing on stderr, closed by `WM_CLOSE`.
- A scripted run of the real `MainWindow` (offscreen, the dark Windows palette emulated; throwaway program in the session
  scratchpad): 3,050 records → table, 41 bars; histogram 120 px, table 512 px; statistics by level (4 groups), by
  message, by field `provider` ("4 groups of 3,050 matches", "(no field)" row); double-click → `provider:"bepaid"`;
  zoom → custom period, Esc → "All time"; export CSV and JSON Lines (1,199 records each, separator `;` from the
  registry); Delete Records Older Than through both dialogs → "Deleted 50 old records." The only Qt messages were
  "This plugin does not support propagateSizeHints()" from the offscreen platform. Screenshots checked: level colors and
  icons readable on the dark palette. Nothing checked on a real screen.

**ASSUMPTIONs**

- Export order is newest first, like the table. CSV header `Time,Level,Source,Message,Fields`, times local as in the
  table (`yyyy-MM-dd HH:mm:ss.zzz`), CRLF; JSON Lines times in UTC (`…Z`, importable back), keys in the order time,
  level, source, message, fields; fields that are not a JSON object stay a string.
- The export target cannot be the open database; the extension decides the format (`.jsonl`/`.json` → JSON Lines).
- Histogram: a click selects nothing; the selection snaps to whole bars; day bars are labeled at UTC midnight.
- The deletion dialog suggests the start of the day 30 days ago; emptied sources stay.
- Statistics: a field grouping without a valid name asks for no statistics (the search never fails because of it).

**Manual checklist for the author** — not run by the author yet. Demo steps 2–7 of DESIGN 11.1:

1. Build, start LogVault, create `demo.lvdb`, Receiver → Start, Live on.
2. `python examples/python_client.py --demo`: a connection in the Receiver panel, records flow in, the histogram grows;
   after 20 s a red spike at the bottom of the bars.
3. Drag across the spike: the period becomes a custom range and the bars narrow; Esc in the histogram goes back.
4. Ctrl+click ERROR; Statistics → Similar messages: `Payment provider timeout after # ms` is almost all of them;
   double-click it → its text in the search line.
5. Double-click `provider` in Record Details → `provider:"bepaid"`; Statistics → Field… `provider` + Enter.
6. Import an old log file (`python examples/python_client.py --demo --file shop.log`, File → Import Files…): progress in
   the status bar, a new source.
7. File → Export Results… (Ctrl+E) → CSV, open it in Excel: columns split, Cyrillic intact. Also try
   Data → Delete Records Older Than… and the dark theme look (selection, alternate rows, icons).

**Known rough edges (for stage 7)**

- CSV cells that start with `=`, `+`, `-` or `@` are read by Excel as formulas (no escaping, per RFC 4180) — decide
  whether to prefix them.
- `Theme::neutralPalette()` and the level button style sheets are computed at start (the palette again on a Windows
  theme switch, the buttons not); the stage 7 theme setting should rebuild both.
- Statistics rows are taller than the records rows (default row height); the stats table could reuse the records
  table's row height.
- `MainWindow.cpp` is about 1,050 lines; settings and the recent list will add more — consider moving the export and
  deletion handlers out if it grows further.

### Stage 7 — Polish (2026-11-20)

All stages are done; the application is version 1.0.0.

**Built**

- `Settings` (`src/gui/Settings.h/.cpp`) — the F16 values (`port`, `wholeNetwork`, `autostartReceiver`,
  `defaultSource`, `timeZone` Local/Utc, `theme` System/Light/Dark, `maxRows` 100–100,000) with `load(QSettings)` /
  `save()`; a missing, damaged or out-of-range value reads as its default. `RecentDatabases` — up to 5 paths, newest
  first, compared without case. Stored in `%APPDATA%\LogVault\LogVault.ini` (`QSettings::IniFormat`, set in
  `main.cpp`); `MainWindow(QSettings&)` gets the store from `main()`.
- `SettingsDialog` — View → Settings… and Receiver → Receiver Settings… (focus on the port); Restore Defaults.
  `MainWindow::applySettings()` applies a change at once: theme, time zone, a running receiver moves to a new
  address/port (`startReceiver()`; clients reconnect), a new default source goes to the server without a restart,
  a new row limit searches again. View → Theme ▸ System/Light/Dark does the same for the theme.
- Theme: `QStyleHints::setColorScheme()` (Qt 6.8; the requirement in `CMakeLists.txt` went from 6.5 to 6.8) asks
  Windows for the light or dark palette, so the title bar follows; `main.cpp` reapplies `Theme::neutralPalette()` on
  `colorSchemeChanged`. `FilterBar` (level button style sheets), `StatsView` (level colors, "(no field)" gray) and
  `RecordDetailsPanel` (level color) rebuild their colors on `QEvent::PaletteChange`.
- Display time zone everywhere: `Format::timestamp/dateTime(msecs, zone)`, `Format::timeZoneName()`; `setTimeZone()`
  on `RecordTableModel` (header "Time (UTC)" in UTC), `RecordDetailsPanel`, `HistogramWidget` (labels and tooltips;
  the static helpers take the zone), `FilterBar` (custom period text, also the ones Esc brings back — the zoom
  history keeps times now, not texts), `ReceiverPanel`; `PeriodDialog` and `DeleteRecordsDialog` take the zone
  (`QDateTimeEdit::setTimeZone()`) and say "Times are in UTC/local time."; `Exporter::start(..., zone)` writes CSV times
  in it (header "Time (UTC)" in UTC).
- F17: geometry, dock/toolbar layout (`saveState(kLayoutVersion)`; the docks and the toolbar have object names), the
  histogram height (saved as a number: the splitter's own state was scaled to a window size not there yet) and the
  records header state; saved in `closeEvent`, restored at the end of the constructor. A header state that hides
  every column shows them all.
- F1: File → Recent Databases ▸ and a list on the placeholder page (file name button + gray folder, elided in the
  middle). A database moves to the top when it opens; a missing one gives a message and is removed (opening it would
  create an empty database).
- Help → About LogVault (`QMessageBox::about`: icon, name, version from `AppInfo`, Qt version). The application icon
  (dark slate tile, three log lines marked green/amber/red) was drawn by a throwaway QPainter program;
  `src/gui/resources/logvault.ico` (16–256 px) is the .exe icon (`LogVault.rc`) and the window icon
  (`resources.qrc`, AUTORCC on the `LogVault` target); `logvault.png` (256 px) is for documents.
- Error messages: `MainWindow::warn(title, text, advice)` — what happened, and below it what to do — for every box
  (open, new database, recent database gone, rename, deletions, import, export, settings file); `reportError(message,
  advice)` for intake and receiver errors (busy port → "close it, or choose another port in Receiver Settings";
  failed batch → "free disk space, locked file; the receiver keeps running"). A UTF-16 import asks to save as UTF-8.
- Rough edges of stage 6: the statistics rows have the records rows' height (`Theme::tableRowHeight()`); the level
  buttons follow the theme. CSV cells starting with `=`, `+`, `-`, `@` are not changed in this stage (see below).
- `README.md` rewritten: features, a diagram, build, run, sending logs, the demo, known limitations, three screenshots
  in `docs/screenshots/` (captures of the real window during the demo: light, Statistics tab, dark).

**Decisions and ASSUMPTIONs**

- Settings live in an INI file, not the registry: easy to show, copy or delete at the defense.
- "Autostart" starts the receiver whenever a database is opened (the window starts on the placeholder page).
- "Whole network" = `0.0.0.0` (IPv4, every interface); Windows Firewall may ask once.
- JSON Lines exports stay in UTC with `Z` (machine-readable, what the import reads back); log lines without a zone are
  still read as local time whatever the display zone.
- CSV cells are still written exactly (RFC 4180): a message starting with `=`, `+`, `-` or `@` may show as a formula in
  Excel (listed in the README). Escaping them with a leading `'` (Message, Source, Fields) is planned for the follow-up
  review after this stage; it changes the exported data, so it was left out here.
- The version is 1.0.0 (`project()` in `CMakeLists.txt`).
- `MainWindow.cpp` is about 1,300 lines of wiring (67 short functions). It was not split in the last stage before
  the demo, to avoid regressions.

**Tests** (the whole suite: 487 passed, 1 skipped — the performance test)

- New class `TestSettings` (8): defaults, a round trip through an INI file, damaged values, derived values (address,
  zone, color scheme), the dialog (round trip, blank default source, Restore Defaults), the recent list (newest first,
  five at most, the same file written differently, removing one missing file among others, cleaning up when read).
- New functions: `TestHistogram::labelsFollowTheTimeZone` (UTC, a +5:30 zone), `TestRecordTableModel::
  showsTimesInTheChosenZone`, `TestExporter::csvTimesFollowTheTimeZone`, `TestFilterBar::
  customPeriodFollowsTheTimeZone`, `periodDialogShowsTheTimeZone`; the histogram, table and exporter tests pass the
  zone explicitly.

**Checks done**

- Clean rebuild of `build/debug`: no warnings; all tests pass (about 20 s). The `release` preset builds without
  warnings. Every commit of the branch builds without warnings and passes the tests (built one by one in a separate
  worktree).
- Smoke run: `LogVault.exe` alive after 5 s with the title "LogVault", nothing on stderr, closed by `WM_CLOSE`, exit
  code 0.
- Scripted runs of the real `MainWindow` on the Windows platform (a throwaway harness in the session scratchpad, its
  own INI file): theme Dark → Light → System (palette and title bar follow, neutral selection kept), the settings
  dialog; the layout of one launch (hidden dock, column width/order/hidden column, histogram 250 px, window size)
  restored in the next; the recent list (missing file → message and removed; an entry from the menu opens and moves to
  the top); About; the demo with the real Python client (steps 1–4, the screenshots); UTC end to end (table header,
  CSV header and times, both dialogs). No Qt warnings in any run.

**DESIGN §0.1 audit**

1. *Errors are never lost* — every `exec`/`prepare`/`COMMIT`, file `open`/`read`/`write`/`commit` and `listen` result
   is checked (grep over `src`); errors reach the user as boxes with advice, intake errors in one shared box; the
   settings file is checked after every save (reported once). Only a `ROLLBACK` that fails after an error is just
   logged (`qWarning`) — the error that caused it is reported. **Fixed here:** the settings file errors, advice in
   every message.
2. *Bad input never crashes* — covered by the tests of stages 2–6 (broken and oversized lines, dropped connections,
   empty and UTF-16 files, busy port, foreign, newer and damaged databases); damaged settings values and saved layouts
   read as defaults. **Fixed here:** a recent database that disappeared is no longer created anew as an empty one; a
   saved header that hides every column.
3. *The UI never freezes* — receiving runs in the intake thread; opening, import, search, export and deletion in the
   pool. The GUI thread waits only when a database is closed or switched (for the current query, import transaction,
   export block or deletion chunk), with a wait cursor.
4. *Clean shutdown* — `closeEvent` saves the layout, then `closeDatabase()`: server stopped and the pending batch
   written (blocking call into the intake thread), importer/exporter/jobs cancelled and awaited, search pool awaited,
   intake thread `quit()` + `wait()`. Smoke run: exit code 0, no warnings.
5. *No data loss* — one transaction per batch; a failed batch is reported; pending lines are written at close and at a
   database switch; a receiver restarted for a new port passes its partial lines on.
6. *Clean code* — no warnings at any commit, `.clang-format` applied, no debug output, TODOs, commented-out code or
   non-English text (checked with grep), comments per §7.3; the core is covered by the tests. Size: `src/core` 3,900
   lines, `src/gui` 4,800 lines (with headers and comments), tests 6,650.

**Demo checklist for the author** (DESIGN §11.1; run it twice in a row)

Preparation: build (`cmake --preset debug && cmake --build --preset debug` with `C:\msys64\ucrt64\bin` first in
`PATH`); `python examples/python_client.py --count 200000 --file old.log` (the file for step 7); start
`build\debug\LogVault.exe` (or `LogVault` in CLion).

1. File → New Database… → `demo.lvdb`: title "LogVault — demo.lvdb", status "Found 0 of 0".
2. Receiver → Start Receiver (Ctrl+R): "Listening on 127.0.0.1:5170"; Live (Ctrl+L): "● Live" at the bottom right.
3. `python examples/python_client.py --demo` in a terminal: a client in the Receiver panel, about 20 lines/s; records
   appear once a second, the histogram grows; after about 20 s red bars appear.
4. Drag across the red bars: the period becomes "… to …" and the bars narrow; click the histogram and press Esc:
   back to "All time".
5. Ctrl+click ERROR; Statistics → Group by: Similar messages: `Payment provider timeout after # ms` ≈ 85–90 %.
6. Records tab: click a payment timeout; in Record Details double-click `provider` → `provider:"bepaid"`; Statistics →
   Field… → `provider` + Enter: one group, `bepaid`, 100 %.
7. File → Import Files… → `old.log` → Import: progress and Cancel in the status bar, then "Added 200,000 records";
   Sources shows a new source `loadtest` (the table keeps the filters of step 6).
8. File → Export Results… (Ctrl+E) → `demo.csv`: "Exported N records…" in the status bar; open it in Excel: the columns
   are split, times readable.
9. Close the window (the client may keep sending).

Second run, right after: start LogVault — same window size and panels, `demo.lvdb` under "Recent databases" on the
start page; click it: all records are there, no messages; repeat steps 2–8 (export to another name). Also try View →
Theme → Light/Dark and View → Settings… → Time zone: UTC (the table header says "Time (UTC)"), then back.

If step 2 says the port is in use, another LogVault or program holds 5170: close it, or change the port in Receiver
Settings (and run the client with `--port`).

**Known limitations** — Windows only, run with MSYS2 in `PATH` (no installer, by decision); JSON and plain text
lines only; zone-less log times are local; statistics by message
over a million records take about 1 s; `MainWindow.cpp` is long (see above).

**For the author**

- Run the demo checklist above twice; the screenshots in `docs/screenshots/` were captured by the agent from the
  real window (replace them with your own if you like).
- DESIGN §12 left the explanatory note (the coursework report) and the UML diagrams for after the application —
  that is the next step.

### Stage 8 — Final review (2026-11-20)

A review of the finished application: the code of stages 4–7 read, and the real window driven through the whole demo
on Windows (dark and light themes) with screenshots — recent databases, receiver and live mode with the Python client,
histogram zoom and Esc, Statistics by level and by similar messages, export to CSV (`;` from the regional settings,
15,778 of 15,778 records) and JSON Lines, deleting records older than a date, a 500,000-line import.

**Fixed**

- CSV injection (DESIGN §6.7): a source, message or fields cell that starts with `=`, `+`, `-`, `@`, a tab or a CR gets
  a leading `'`; JSON Lines is unchanged. Test `TestExporter::csvDefusesFormulas`.
- Opening another database or closing the window while an import or export runs asks first ("Open Anyway" / "Exit
  Anyway", Cancel by default) and says what stops and what stays (`MainWindow::confirmCancellingTasks()`).
- Exit during an import: the "import was cancelled" box appeared after the window had gone, and the process stayed
  alive invisibly once it was dismissed. While the window closes (`m_closing`), the import, export and job results and
  intake errors are now ignored; the exit is clean, the database passes `PRAGMA integrity_check`.

**Checks done** — no warnings, all tests pass (488 incl. the new one, 1 skipped — the performance test); every manual
step above repeated after the fixes.

**Left as is** (documented, no action needed): case-insensitive search for Latin letters only (SQLite `LIKE`); a
running query of about a second is not interrupted when a newer search starts; `MainWindow.cpp` is long (wiring only).
