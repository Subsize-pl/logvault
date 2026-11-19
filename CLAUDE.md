# LogVault — instructions for AI agents

LogVault is a Windows desktop application (C++20, Qt 6 Widgets) that receives logs over TCP and from files, stores them
in SQLite and lets the user search, filter and analyze them. It is a university coursework project.

- Full design: `docs/DESIGN.md` — the single source of truth.
- Progress and handoff notes: `docs/PROGRESS.md`.

## How a session works

1. Each session implements **exactly one stage** from `docs/DESIGN.md` §9, in order. Take the first stage that is not
   `done` in `docs/PROGRESS.md` and read the handoff notes left by the previous session.
2. Read `docs/DESIGN.md` §0 (rules and quality), §7.3 (code style) and the sections listed for the stage in §9.
3. Work on a branch `stage-N-<name>` (e.g. `stage-2-storage`). When the stage is done, merge it into `main` and push both.
4. Finish only when: the build has no warnings, all tests pass, the stage's manual check is done, and `docs/PROGRESS.md`
   is updated (status, what was built, decisions and `ASSUMPTION:`s, notes for the next stage).
5. If the design has to change, update `docs/DESIGN.md` in the same commit and mention it in the report.

## Communication

- Talk to the author in **Russian**. Everything in the repository — code, comments, UI text, documentation, commit
  messages — is in **English**.
- The author has little time: keep reports short — what was done, how to check it, what (if anything) they must decide.
- Ask before adding features beyond `docs/DESIGN.md` §1.2 or any third-party library besides Qt.

## Environment

- Toolchain: MSYS2 UCRT64 in `C:\msys64\ucrt64` — GCC, CMake, Ninja, Qt 6 (`qt6-base`). Exact versions are in
  `docs/PROGRESS.md`.
- Put `C:\msys64\ucrt64\bin` first in `PATH` before building or running:
  - Git Bash: `export PATH="/c/msys64/ucrt64/bin:$PATH"`
  - PowerShell: `$env:Path = "C:\msys64\ucrt64\bin;$env:Path"`
- Build and test (available from stage 1 on):

  ```bash
  cmake --preset debug && cmake --build --preset debug && ctest --preset debug
  ```

- To install a missing MSYS2 package, run pacman through MSYS2's bash from a script file (inline quoting breaks in
  PowerShell): `C:\msys64\usr\bin\bash.exe -l /path/to/script.sh`.

## Rules (details in `docs/DESIGN.md`)

- Quality over size (§0.1): check the result of every network, file and database operation; bad input never crashes
  the app; the UI never freezes; clean shutdown; no data loss.
- `src/core` never includes QtWidgets. All SQL lives in `LogStore`. Values reach SQL only through bound parameters.
- One database connection per thread.
- Comments: English, a reasonable amount — a short `///` per class, `///` on public methods only when the name is not
  enough, `//` inside functions to explain *why*. No commented-out code.
- Commits: Conventional Commits (`feat:`, `fix:`, `test:`, `docs:`, `refactor:`, `chore:`), one logical change per
  commit, the project builds at every commit.
