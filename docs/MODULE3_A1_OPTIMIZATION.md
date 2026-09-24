# Module3 A1 Low-Risk Optimization

## Scope and reference

This round starts from `perf/module3-arm` at `beff0f0` and changes only `src/module3/cfo.c` and `src/module3/dsp.c`. Windows GNU 16.1 Release builds used `WRJ_ENABLE_PROFILING=ON`. The independent reference build and its 35-row SHA-256 manifest are under `build-pc/windows/a1-baseline/`; the final build is under `build-pc/windows/a1-final/`. Both directories are ignored build artifacts.

The user-provided ARM profiling established the original hotspots. The timings below are single Windows runs for a correctness and direction check. They are not ARM speedup measurements. ARM benefit must be measured on the board with the same five cases and build configuration.

## Changes and stepwise verification

1. **FIR valid-tap bounds** — `m3_bandlimit_fir()` in `src/module3/cfo.c` computes the first and last valid tap once per output sample. The inner loop no longer checks every tap against the IQ bounds. Tap generation, coefficients, double accumulators, ascending tap order, valid-sample additions, and zero-padding are unchanged. The four required cases produced **24/24 byte-identical formal CSVs** versus the reference. Commit: `56e5e5c`.
2. **Wideband energy window reuse** — `m3_select_wideband_numerology()` in `src/module3/dsp.c` selects the maximum-energy input window once, before testing the five numerologies. Window count, candidate starts, double energy sum, strict `>` tie rule, CP metrics, scores, and candidate order are unchanged. Autel001 and DJI018 produced **12/12 byte-identical formal CSVs**. Their `module3_result.csv`, frame starts, frame length, CFO, and Module4 bytes are unchanged; frame lengths remain 1152 and 2208 samples, respectively. Commit: `4787325`.
3. **Integer CFO invariant terms** — `m3_integer_cfo_search()` in `src/module3/cfo.c` calculates each IQ pair's magnitudes, fourth-power phases, and weight once, then updates the seven candidate sums. Each candidate's sum still visits IQ pairs in the same order. Rotation formula, `k=-3..3`, sample stride 4, double math, scoring order and formula, and the 0.08 acceptance rule are unchanged. DroneID013, Autel001, and DJI018 produced **18/18 byte-identical formal CSVs**, including the CFO fields, frame starts, and Module4 bytes. Commit: `623b456`.

Each step used a separate clean Release build: `a1-fir`, `a1-wideband`, and `a1-integer`. The SHA-256 comparisons covered `module3_result.csv`, `module3_frames.csv`, `module4_result.csv`, `module4_fields.csv`, `module4_packets.csv`, and `module4_bytes.csv` for each applicable case.

## Windows profiling at each optimization checkpoint

Time is in milliseconds. “After” is the build immediately after that individual change, with preceding approved changes retained. Speedup is before divided by after.

| Stage | Case | Before | After | Speedup |
|---|---|---:|---:|---:|
| FIR | DroneID013 | 48.511 | 41.865 | 1.16× |
| FIR | RemoteID005 | 79.241 | 73.374 | 1.08× |
| FIR | Autel001 | 145.442 | 125.527 | 1.16× |
| FIR | DJI018 | 197.036 | 150.127 | 1.31× |
| PROFILE_SELECTION | Autel001 | 812.295 | 811.075 | 1.00× |
| PROFILE_SELECTION | DJI018 | 841.447 | 830.748 | 1.01× |
| INTEGER_CFO | DroneID013 | 29.102 | 7.618 | 3.82× |
| INTEGER_CFO | Autel001 | 88.894 | 23.923 | 3.72× |
| INTEGER_CFO | DJI018 | 107.378 | 28.263 | 3.80× |

The Wideband window change saves little in these Windows measurements because most `PROFILE_SELECTION` time remains in candidate-specific CP scoring and quantiles. Single-run timer variation also affects small differences; the final Autel selection time was 812.622 ms, slightly above the 812.295 ms reference.

## Full Windows regression

The final clean Release build passed CTest **1/1**. All five cases produced **30/30 SHA-256-identical formal CSVs** against `a1-baseline`. The 16 operation-count rows per case in `profiling_module3.csv` also match the reference (80/80). Timing rows are expected to differ.

| Case | M3 total before (ms) | M3 total after (ms) | Speedup |
|---|---:|---:|---:|
| DroneID013 | 280.552 | 253.627 | 1.11× |
| RemoteID005 | 1416.736 | 1415.476 | 1.00× |
| Autel001 | 2028.481 | 1880.509 | 1.08× |
| DJI018 | 2098.339 | 1830.837 | 1.15× |
| RemoteID028 | 11840.914 | 11733.473 | 1.01× |

RemoteID028 still follows the original nine-candidate residual-CFO path; its result remains `crc_failed` in this C port, exactly as in the reference build. No Module4 source, protocol recovery rule, sync threshold, CFO candidate range, or build optimization setting changed. The baseline tag `arm-m34-baseline-v1` remains at `d4660b2`.

## ARM follow-up

Build this branch on Ubuntu with profiling enabled, deploy the same five cases, and compare each board result against the existing ARM baseline before assessing speedup. Record the board's stage times and binary hash alongside the run metadata. The Windows times above do not establish an ARM improvement.
