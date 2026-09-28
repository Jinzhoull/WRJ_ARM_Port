# Module3 A1–A4 Optimization Summary

## Scope and validation boundary

A1–A4 were bounded Module3 performance changes. They retained the existing algorithms, search ranges, thresholds, data interfaces, and Module4 behavior. Windows Release/profiling builds were used during development; the consolidated ARMv7 acceptance is recorded in [ARM_MODULE3_FINAL_VALIDATION.md](ARM_MODULE3_FINAL_VALIDATION.md). Timing figures are environment-specific and are not claims of protocol accuracy improvement.

## Optimization rounds

| Round | Change | Correctness evidence | Main measured effect |
|---|---|---|---|
| A1 | Reuse FIR valid-tap bounds, select the Wideband energy window once, and hoist invariant terms from the seven-way integer-CFO search. | Five representative cases: 30/30 formal CSV hashes and 80/80 operation-counter rows matched the reference; CTest 1/1. | Windows M3 total improved 1–15% for four cases; integer-CFO stage improved about 3.7–3.8× in sampled runs. |
| A2 | Reuse FFT stage twiddles and precompute the existing Hann weights once per spectral analysis. | 30/30 CSV hashes and 80/80 counter rows matched; FFT call counts unchanged; CTest 1/1. | Theoretical trigonometric-call reductions: about 86.5% for FFT twiddles and 93.75% for Hann coefficients across the five cases. These are operation-count estimates, not ARM timing claims. |
| A3 | Cache the exact per-sample BLE phase increments for low-SNR CRC recovery. | 30/30 CSV hashes matched; all search counters unchanged; RemoteID005 retained four CRC24-valid PDUs and UAS000185. | Windows M3 time decreased 72.44% for RemoteID005 and 91.94% for RemoteID028 in the recorded runs. Workspace grew by 7 MiB, to 36.06 MiB. |
| A4 | Generate the BLE whitening mask once per synchronization call and reject invalid decoded lengths after the header instead of completing all soft-bit work. | 30/30 formal CSV hashes and 85/85 operation-counter rows matched A3; CTest 1/1. | Additional Windows BLE recovery reductions: 30.03% for RemoteID005 and 31.58% for RemoteID028 in the recorded run. No new heap allocation; 216 bytes additional local stack. |

The A1–A4 code changes were intended to reduce repeated work without changing decisions. CSV identity and operation counts are the acceptance evidence; small timer differences should not be overinterpreted.

## Consolidated ARMv7 acceptance

The recorded ARM test used a static ARMv7 hard-float Release build at `-O2`, profiling enabled for the comparison, on a board with about 497 MiB RAM. Against the earlier build, all 30 formal Module3/4 CSV files across five cases matched byte-for-byte. All five cases improved measured Module3 total time: DroneID013 16.53%, RemoteID005 79.56%, Autel Wideband001 17.43%, DJI Wideband018 2.88%, and RemoteID028 93.66%. Peak RSS ranged from about 9.7 to 23.7 MiB; no crash or OOM was observed.

RemoteID005 remained successfully parsed with four CRC24-valid distinct PDUs. RemoteID028 remained `crc_failed` with zero recovered bytes despite its large runtime reduction. Wideband and DroneID cases remain partial parses; performance optimization does not imply protocol-level validation. See the linked ARM report for exact stage timings, binary hashes, and test conditions.

## Current status

The A1–A4 work is documented as validated on the recorded optimization revision. This cleanup does not alter `src/module3/` or `src/module4/`. Current checkout and working-tree state are recorded in [PROJECT_CLEANUP.md](PROJECT_CLEANUP.md); do not infer that the archived ARM timing comparison was rerun on the current checkout.

## Current Windows Release benchmark

On 2026-09-28, the current `main` revision `043934a` was freshly configured as MinGW Release `-O2 -DNDEBUG`, with `WRJ_ENABLE_PROFILING=OFF`; CTest passed 1/1. The 11 standard cases were each timed three times. A temporary entry-point timer measured `m3_run`/`m4_run`; it was removed before rebuilding the final binary. Module3 remained `ok` on 11/11 cases. Mean Module3 time ranged from 252.926 ms (DroneID013) to 6269.468 ms (Control004); mean Module4 time ranged from 0.012 ms (RemoteID028 CRC short-circuit) to 141.683 ms (RemoteID005). Full per-case timing and parse outcomes are in [the benchmark report](../build-pc/windows/latest/performance/performance_report.md) and [CSV](../build-pc/windows/latest/performance/performance_summary.csv). These are PC timing results, not accuracy claims.
