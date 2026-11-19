# Progress

One stage per session, in order (see `docs/DESIGN.md` §9). Update this file at the end of every session.

| # | Stage | Status | Date |
|---|---|---|---|
| 0 | Preparation | done | 2026-11-19 |
| 1 | Skeleton | done | 2026-11-19 |
| 2 | Storage | **next** | |
| 3 | Intake | — | |
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
- `.clang-format` (LLVM, indent 4, column limit 110); "Building" section in `README.md`.

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
  The LLVM base formats pointers and references as `Type *name` and indents `public:` by 2, unlike the `Type& name`
  in the §7.3 sample; asked the author whether to add `PointerAlignment: Left` and `AccessModifierOffset: -4`.
