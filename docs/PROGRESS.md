# Progress

One stage per session, in order (see `docs/DESIGN.md` §9). Update this file at the end of every session.

| # | Stage | Status | Date |
|---|---|---|---|
| 0 | Preparation | done | 2026-11-19 |
| 1 | Skeleton | done | 2026-11-19 |
| 2 | Storage | done | 2026-11-19 |
| 3 | Intake | **next** | |
| 4 | Search | — | |
| 5 | Main window | — | |
| 6 | Analysis | — | |
| 7 | Polish | — | |

## Environment

- MSYS2 UCRT64 (`C:\msys64\ucrt64`), updated 2026-11-19. Installed: GCC 16.2.0, CMake 4.5.0, Ninja 1.13.2,
  Qt 6.12.0 (`qt6-base`), SQLite 3.54.1.
- Verified with a throwaway CMake project: Qt compiles and runs, the `QSQLITE` driver is available,
  `json_extract` works.
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
