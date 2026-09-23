# Development Workflow

## Roles and source synchronization

- Windows is the primary source editing and PC validation environment.
- GitHub is the only source synchronization channel between Windows and Ubuntu. Do not copy the source tree by USB, shared-folder overwrite, or SCP.
- Ubuntu pulls reviewed commits, cross-compiles for ARMv7, and deploys the program. `analog-board` is for final execution and performance measurements.
- Do not edit Module3/Module4 algorithms on Windows and Ubuntu at the same time. Do not make algorithm edits directly on Ubuntu; record findings and fix them on Windows.
- The checked-in 11 CF32 inputs and handoff CSV travel through Git. Board inputs and generated board outputs continue to use the deployment scripts.

## Windows development

Start from the project root:

```powershell
git status
git pull --ff-only origin main
tools\build_pc.bat
```

Run the built-in tests when validating a change:

```powershell
$env:RUN_TESTS = '1'
tools\build_pc.bat
Remove-Item Env:RUN_TESTS
```

Run relevant sample cases with outputs in a new temporary directory under `build-pc\windows\`; do not overwrite archived `results\` runs. Before committing, inspect `git diff` and `git status`, then stage only the intended files:

```powershell
git add <files>
git diff --cached
git commit -m "perf: describe the change"
git push origin main
```

Use simple commit prefixes: `feat:`, `fix:`, `perf:`, `build:`, `docs:`, `test:`, or `refactor:`. Keep `main` as the default branch. For a larger isolated effort, use `perf/<topic>` and merge it after PC validation.

Do not commit build trees, binaries, logs, credentials, or unreviewed results. Do not push algorithm changes before the relevant PC validation passes.

## Ubuntu to ARMv7 board

Before pulling, check for local changes. For the established checkout:

```sh
cd ~/WRJ_ARM_Port
git status
git pull --ff-only origin main
./tools/build_arm.sh
```

Deploy an explicit candidate and its IQ input, then run it:

```sh
HANDOFF_FILE=data/module12_to_module3_handoff.csv \
IQ_FILE=data/cases/sim_dji_droneid_013_M001.cf32 \
./tools/deploy_board.sh

HANDOFF_NAME=data/module12_to_module3_handoff.csv \
CANDIDATE_ID=sim_dji_droneid_013_M001 \
IQ_NAME=data/sim_dji_droneid_013_M001.cf32 \
./tools/run_board_test.sh
```

The scripts use the `analog-board` SSH alias and `tools/board_config.sh`; credentials and private keys stay in the user's SSH configuration and are never copied into this repository. Board results use timestamped directories under `results/arm/`.

The current Ubuntu checkout at `~/WRJ_ARM_Port` may contain files or local edits unique to that machine. Check and back up anything unique before replacing it with a clean clone. Never delete or overwrite it remotely as part of Windows development.

## Initial private GitHub setup

Create an empty **private** repository named `WRJ_ARM_Port`, add it as `origin`, then push `main` and the baseline tag. Never put a GitHub password or token in chat, source, or shell history. After the remote is connected, verify `git status` and `git branch -vv` on both Windows and Ubuntu.
