# Module3 ARM Performance Profiling Plan

## Scope and current status

This branch adds measurement only. `WRJ_ENABLE_PROFILING` defaults to `OFF`; when disabled, timer/count macros expand away and the profiler source is not linked. When enabled, timing uses `QueryPerformanceCounter` on Windows and `clock_gettime(CLOCK_MONOTONIC)` on Linux. Profiling writes a separate `profiling_module3.csv` in the case output directory after the normal result CSVs. No Module3/4 result schema changes are made.

The M3 total timer covers `m3_run()` only, excluding handoff/IQ file reads, workspace allocation, Module4, and result writing. Allocation counters cover the 15 M3 workspace `calloc` calls and matching `free` calls. Stage percentages use M3 total as denominator; nested stages are inclusive and can overlap, so percentages are not additive. `OTHER` is a residual: total time minus the measured non-overlapping common stages and the selected top-level sync path, clamped to zero. Unexecuted stages and operations are emitted as zero rows. No printf is added to hot loops.

## Call paths

```text
main
  ├─ load Module1/2 handoff; allocate M3 workspace; read CF32 IQ
  └─ m3_run
      ├─ m3_select_profile
      ├─ m3_preprocess_iq
      ├─ candidate-center CFO rotation
      ├─ [DJI/Autel Wideband] m3_select_wideband_numerology
      ├─ m3_estimate_spectral_center → spectral CFO rotation
      ├─ m3_bandlimit_fir
      ├─ [OFDM profiles] m3_integer_cfo_search → optional CFO rotation
      └─ profile-specific path
```

- **Wideband (DJI/Autel):** numerology selection tests five `(Nfft, CP)` pairs over a bounded input window; spectrum-center/CFO correction follows; integer-CFO search tests `k=-3..3`; `m3_cp_synchronize` runs twice, with CP-derived fractional-CFO correction between passes; CP frame refinement and SFO estimation finish the path.
- **DroneID:** common front-end and integer-CFO stages; `m3_droneid_synchronize` first runs CP synchronization as fusion evidence, generates the ZC600 and ZC147 templates, scores candidate frame boundaries, performs coarse 8-sample then local 1-sample offset search, builds the frame grid, and estimates SFO.
- **RemoteID BLE:** common front-end; `m3_remoteid_ble_synchronize` smooths IQ, computes a four-lag GFSK discriminator, searches all valid BLE sample phases for preamble/access-address evidence, keeps separated packet candidates, and runs timing/window/CRC hypotheses. If the first pass has no CRC-valid PDU, `m3_run` tries nine residual-CFO offsets; each trial rotates the full IQ buffer and reruns the BLE path.
- **Unknown/Control:** Control burst uses 256-sample energy blocks; Unknown and the evidence fallback use blind CP repetition across seven lag choices and three CP ratios, i.e. up to 21 CP profiles per blind pass.

## Profiling CSV fields

`profiling_module3.csv` columns are `case,stage,calls,time_ms,percentage`. Stage rows cover total, preprocessing, center shift, spectrum/profile selection, coarse/integer/fractional CFO, CFO compensation, FIR, Wideband/DroneID/BLE synchronization, DroneID ZC/CP fusion, BLE preamble/phase/low-SNR/residual-CFO search, frame alignment, Unknown/Control, and other. Operation rows use the same schema with `time_ms=0` and `percentage=0`; they report FFT/DFT, correlation calls/evaluations, complex rotations/calls/samples, CFO hypotheses, BLE phases/residual candidates/hypotheses, CRC checks, frame-candidate positions, and malloc/calloc/free counts. Correlation and frame-candidate counts include evaluated sample offsets and protocol hypotheses, not only accepted frames.

## Windows profiling parity check

Build into separate ignored directories to avoid CMake-cache cross-contamination:

```powershell
$env:PC_BUILD_DIR = 'build-pc\windows\profiling-off'
$env:WRJ_ENABLE_PROFILING = 'OFF'
$env:RUN_TESTS = '1'
cmd /c 'call tools\build_pc.bat'

$env:PC_BUILD_DIR = 'build-pc\windows\profiling-on'
$env:WRJ_ENABLE_PROFILING = 'ON'
cmd /c 'call tools\build_pc.bat'
```

Run only `sim_dji_droneid_013_M001`, `sim_remoteid_ble_005_M001`, and `sim_dji_wideband_018_M001` from `data/cases/`, directing outputs into each build directory. Compare SHA-256 for `module3_result.csv`, `module3_frames.csv`, `module4_result.csv`, `module4_fields.csv`, `module4_packets.csv`, and `module4_bytes.csv`. The ON output should additionally contain `profiling_module3.csv`; OFF must not create it.

Validation on Windows GNU 16.1, Release: CTest passed 1/1. For all three requested cases, all 18 formal CSV files (3 cases × 6 files) were byte-identical between profiling OFF and ON. Profile ON reported:

| Case | M3 total (PC) | Highest stages (inclusive; ms) |
|---|---:|---|
| DroneID 013 | 272.210 | DroneID sync 76.277; CP fusion 73.816; coarse CFO 66.061; spectrum analysis 63.220; IQ preprocess 52.934 |
| RemoteID 005 | 1390.296 | BLE sync 1123.700; low-SNR recovery 1011.862; preamble/AA 111.772; coarse CFO 92.938; IQ preprocess 91.845 |
| DJI Wideband 018 | 1963.303 | profile/numerology selection 802.892; Wideband sync 581.685; IQ preprocess 201.517; FIR 164.152; integer CFO 100.797 |

These PC values validate instrumentation and are not ARM performance measurements. Representative operation counters from the same runs:

| Case | FFT calls | DFT | Correlation calls / evaluations | Rotations (calls / IQ samples) | BLE phases | BLE CFO trials | BLE hypotheses / CRC checks | alloc/free |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| DroneID 013 | 96 | 0 | 639 / 1,508,415 | 2 / 407,964 | 0 | 0 | 0 / 0 | 15 / 15 `calloc`/`free`; 0 `malloc` |
| RemoteID 005 | 21 | 0 | 338,592 / 32,504,832 | 2 / 687,104 | 31 | 0 | 5,828 / 2,875 | 15 / 15 `calloc`/`free`; 0 `malloc` |
| DJI Wideband 018 | 40 | 0 | 7 / 2,767,863 | 3 / 2,203,200 | 0 | 0 | 0 / 0 | 15 / 15 `calloc`/`free`; 0 `malloc` |

The owner-provided, non-instrumented ARM `-O2/static` baseline remains the reference and has not been rerun by this change:

| Case | ARM Module3 baseline | ARM total baseline |
|---|---:|---:|
| DroneID 013 | 4.347 s | 6.011 s |
| RemoteID 005 | 34.215 s | 38.172 s |
| Autel Wideband 001 | 19.340 s | 19.825 s |
| DJI Wideband 018 | 22.344 s | 23.014 s |
| RemoteID 028 | 288.967 s | 289.135 s |

## Static repeated-work inventory (analysis only)

No optimization is implemented in this profiling-only change.

| File / function | Loop nesting and repeated work | Invariant relative to loop variable? | Reuse potential |
|---|---|---|---|
| `src/module3/dsp.c` / `m3_select_wideband_numerology` | For each of five numerologies, reselects the highest-energy one of up to seven windows before scanning CP metrics. | Window length, count, and energy ranking depend on IQ/count, not `(Nfft, CP)`. | Strong A-level candidate: choose the window once and reuse its start; retain separate CP metrics. |
| `src/module3/fft.c` / `m3_fft_forward_radix2` | `sin/cos` twiddles are evaluated for each butterfly offset inside every `base` block. | Twiddle angle depends on `(span, offset)`, not `base`. | Strong A-level candidate: precompute one stage's twiddles and reuse identical float values. |
| `src/module3/cfo.c` / `m3_integer_cfo_search` | Seven CFO hypotheses each recompute input magnitudes and fourth-power phases at every 4th sample, then apply the hypothesis rotation. | Magnitudes and unrotated sample phases do not depend on integer candidate `k`; only `delta` does. | Strong A-level candidate: cache candidate-independent per-sample terms, then preserve candidate scoring order. |
| `src/module3/cfo.c` / `m3_estimate_spectral_center` | Up to 20 overlapping blocks compute FFTs and recreate a Hann window with `cosf` for every bin. | Window coefficient depends on FFT length/bin, not block. | A-level candidate: reuse the exact window values. FFT inputs remain block-dependent. |
| `src/module3/profiles.c` / DroneID ZC scoring | Two templates are generated once and reused; each candidate offset still recomputes two dot products and template energy. | Template energy is independent of candidate frame offset; signal energy/correlation are not. | A-level candidate: precompute template norms only; sample-window work remains candidate-dependent. |
| `src/module3/profiles.c` / BLE acquisition setup | Expected access-address/fixed-bit signs and whitening mask are rebuilt on each BLE call, including residual-CFO retries. | These fixed channel-38 constants do not depend on IQ/CFO candidate. | Low-risk A-level candidate: use immutable precomputed tables. Small expected saving; measure first. |
| `src/module3/profiles.c` / `m3_ble_crc_attempt` | Each timing/window hypothesis redoes lagged phase `atan2` across the sync and data bit sampling windows before CRC. RemoteID 005 made 5,828 hypotheses and 2,875 CRC checks on PC. | Within one BLE call the IQ/lag is fixed, but timing/window hypotheses select different sample ranges; residual-CFO retries change IQ. | High potential, precision-sensitive: investigate cached per-sample phase increments while preserving accumulation order; do not implement before ARM data and output parity tests. |
| `src/module3/module3.c` / RemoteID residual-CFO loop | Up to nine candidate offsets each rotate the full IQ and rerun BLE smoothing, discriminator, phase/access-address scan, and CRC recovery. | Candidate CFO changes the input, so most receiver work is candidate-dependent. Fixed BLE tables are not. | Candidate pruning is C-level and forbidden in this phase. Later consider exact, reusable fixed setup only. |
| `src/module3/cfo.c` / `m3_cfo_compensate` | Every call evaluates `sin/cos` per IQ sample; RemoteID fallback repeats this across CFO candidates. | Rotation angle depends on correction CFO and sample index. | A-level only if exactly identical rotator values can be reused without altering arithmetic; otherwise B-level. |
| `src/module3/module3.c` / `m3_workspace_init/release` | 15 `calloc` allocations before processing and matching frees afterward. | Not inside candidate/profile/phase loops. | No hot-loop allocation detected; do not optimize absent ARM memory evidence. |

Static math hotspots include `sin/cos` in each CFO sample rotation and FFT butterfly, `atan2` in integer-CFO scoring and BLE recovery hypotheses, `sqrt` in CP/correlation normalization, and `atan2f` across four BLE discriminator lags. Module3 has no DFT implementation. Profiling counters show no per-frame heap allocation; only the 15 workspace `calloc`s and frees occur per process/case.

## Provisional opportunity grades

- **A — low risk, first to evaluate on ARM:** (1) reuse Wideband energy-window selection; it was the largest PC stage in DJI Wideband 018; (2) precompute FFT twiddles for each `(span, offset)`; (3) compute candidate-independent integer-CFO magnitude/phase terms once. Each should preserve the same formulas and search domain, but still requires byte-for-byte result validation.
- **B — medium risk:** cache/reuse BLE phase-discriminator terms across CRC timing/window hypotheses; replace repeated transcendental calls while preserving soft-bit thresholds; alter numeric storage/float precision.
- **C — algorithmic and out of scope:** reduce BLE hypotheses or residual CFO candidates, add early stop/pruning, change sync thresholds/profile competition, or alter CRC search policy.

These are hypotheses pending actual ARM stage data. Do not implement any of them in the profiling-only commit.

## Ubuntu/ARM collection instructions

After this branch is pushed, on the Ubuntu build host:

```sh
cd ~/WRJ_ARM_Port
git status
git fetch origin
git switch perf/module3-arm
git pull --ff-only origin perf/module3-arm
```

If the local branch does not exist yet, use `git switch -c perf/module3-arm --track origin/perf/module3-arm`. Build with profiling enabled; this passes only the profiling macro in addition to the existing ARM Release `-O2`, static link, ARMv7 hard-float toolchain settings:

```sh
WRJ_ENABLE_PROFILING=ON ./tools/build_arm.sh
```

Run the five cases in this exact order. The helper deploys the current ARM binary, handoff, and one IQ file per case. The timestamped `OUT_NAME` default prevents overwriting archived results. **Run RemoteID 028 exactly once** because it takes about 289 seconds on ARM.

```sh
run_profile_case() {
    case_id="$1"
    HANDOFF_FILE=data/module12_to_module3_handoff.csv \
    IQ_FILE="data/cases/${case_id}.cf32" ./tools/deploy_board.sh
    HANDOFF_NAME=data/module12_to_module3_handoff.csv \
    CANDIDATE_ID="${case_id}" \
    IQ_NAME="data/${case_id}.cf32" ./tools/run_board_test.sh
}

run_profile_case sim_dji_droneid_013_M001
run_profile_case sim_remoteid_ble_005_M001
run_profile_case sim_autel_wideband_001_M001
run_profile_case sim_dji_wideband_018_M001
run_profile_case sim_remoteid_ble_028_M001   # exactly once
```

Each copied case directory should include `profiling_module3.csv`; the matching board console log remains under `results/arm/logs/`. Do not stage generated case results or logs. After the five runs, transfer the five profiling CSVs through the normal Git workflow or a reviewed artifact bundle, compare output CSVs against the existing `results/arm/first-run-20260923-o2` baseline, then create `docs/ARM_MODULE3_PROFILE.md` with per-case M3 total, top ten timed stages, top ten operation counts, RemoteID005/028 path differences, and the repeated-work conclusions. ARM timings and rankings must come from these files, not the PC figures above.
