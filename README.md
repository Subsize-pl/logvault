# LogVault

A desktop application for collecting, storing and exploring logs (Windows, C++20, Qt 6).

- Receives logs over TCP — one line per record, JSON or plain text — and imports log files.
- Stores everything in a single SQLite database file.
- Search, level/source/period filters, a time histogram, statistics, live mode and export.

> **Status:** in development. Design: [docs/DESIGN.md](docs/DESIGN.md) · Progress: [docs/PROGRESS.md](docs/PROGRESS.md)

## Building

### Prerequisites

Windows 10/11 with [MSYS2](https://www.msys2.org) installed in `C:\msys64`. In the *MSYS2 UCRT64* shell, install the
compiler, CMake, Ninja and Qt 6:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base
```

The versions the project is developed with are listed in [docs/PROGRESS.md](docs/PROGRESS.md).

### CLion

One-time setup: *Settings → Build, Execution, Deployment → Toolchains → + → MinGW*, toolset `C:\msys64\ucrt64`,
then move it to the top of the list to make it the default.

CLion shows the `debug` and `release` presets from `CMakePresets.json` as CMake profiles
(*Settings → Build, Execution, Deployment → CMake*; enable `debug` there if it is disabled). Run the `LogVault`
configuration to start the application and `lv_tests` to run the tests.

### Command line

`C:\msys64\ucrt64\bin` must come first in `PATH`. Git Bash:

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

PowerShell:

```powershell
$env:Path = "C:\msys64\ucrt64\bin;$env:Path"
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

An optimized build uses the `release` preset: `cmake --preset release`, then `cmake --build --preset release`.
Build files go to `build/<preset>`.

### Running

The application loads the Qt and compiler DLLs from `C:\msys64\ucrt64\bin` (there is no deployment step), so start
it from CLion or from a shell where that folder is first in `PATH`:

```bash
./build/debug/LogVault.exe
```
