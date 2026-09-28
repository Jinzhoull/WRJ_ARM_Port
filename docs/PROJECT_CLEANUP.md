# Project Cleanup Record

## Cleanup scope

This cleanup consolidates experiment results and duplicate optimization reports. It does not edit `src/module3/` or `src/module4/`, rebuild the project, or commit Git changes. Historical PC and ARM result batches are copied as nested subdirectories to prevent same-named case files from overwriting one another. Recursive directory deletion was explicitly requested, but the local execution policy rejected `Remove-Item -Recurse`; original experiment and result source directories therefore remain in place pending a permitted cleanup mechanism.

## Removed experiment directories

Copied the current final Release validation to `build-pc/windows/latest/`. Deletion of the requested A1–A4 experiment directories (`a1*`, `a2*`, `a3*`, `a4*`), BLE/CFO/Control `*-trial*` directories, `profiling-off`, `full-correctness-validation-before*`, and `failed-nmake-first-config` was blocked by the execution policy. Those source directories remain under `build-pc/windows/`; no alternate deletion mechanism was used.

Removed the seven source reports after consolidating their content:

- `MODULE3_A1_OPTIMIZATION.md` through `MODULE3_A4_OPTIMIZATION.md` → [MODULE3_OPTIMIZATION.md](MODULE3_OPTIMIZATION.md).
- `CFO_ALIGNMENT_FIX_REPORT.md`, `CFO_FULL_ALIGNMENT_REPORT.md`, and `FINAL_CFO_AND_RECOVERY_FIX_REPORT.md` → [CFO_ALIGNMENT_REPORT.md](CFO_ALIGNMENT_REPORT.md).

## Result copies and pending source cleanup

The requested canonical copies now exist at:

- `results/pc/latest-validation/`: `module3_initial/`, `module3_regression/`, and `module4_regression/` retain their original batch hierarchy.
- `results/arm/latest-validation/`: the five archived ARM first-run cases, including their original per-case files/logs.

The Windows final validation copy contains 11 case directories, six CSV outputs per case, `final_comparison.csv`, and all 67 source files. The three old PC batch directories and the old ARM batch directory still remain because recursive deletion was blocked; their copies were verified file-by-file with SHA-256 before any cleanup attempt.

## Current project structure

```text
WRJ_ARM_Port/
├── build-pc/windows/           # Latest copy plus experiment/build directories (source cleanup pending)
│   └── latest/                 # Final 11-case Windows Release validation
├── cmake/                      # ARMv7 toolchain configuration
├── data/cases/                 # Candidate IQ inputs
├── docs/                       # Build, architecture, validation and optimization reports
├── include/                    # Public C interfaces and shared types
├── results/
│   ├── pc/                     # latest-validation copy plus original batches (cleanup pending)
│   └── arm/                    # latest-validation copy plus original batch (cleanup pending)
├── src/module3/ src/module4/   # C implementations
├── tests/                      # C tests
└── tools/                      # Build, deployment and validation scripts
```

## Version and working-tree status

At cleanup time, the checkout is branch `main` at `2e78104` (`docs: report Module3 CFO alignment findings and regression`). The annotated tag `arm-m34-baseline-v1` still peels to `d4660b2`. The working tree already contained uncommitted changes in `src/module3/cfo.c` and `src/module3/module3.c`; those source changes were preserved. No commit was created. The final Release run records 11/11 case executions and CTest 1/1, with RemoteID005 parsed and RemoteID028 still `crc_failed`.
