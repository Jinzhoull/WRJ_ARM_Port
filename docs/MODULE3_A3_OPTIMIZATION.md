# Module3 A3 Optimization: BLE Recovery Phase Cache

## Scope and rationale

The prior ARM profile identified RemoteID BLE low-SNR CRC recovery as the dominant cost: RemoteID005 spent 26,514 ms of 28,190 ms `BLE_SYNC` there (5,828 hypotheses, 2,875 CRC checks); RemoteID028 spent 273,425 ms of 282,328 ms `BLE_SYNC` there (60,480 hypotheses, 29,196 CRC checks, nine residual-CFO trials). This A3 change targets only repeated `atan2` in `m3_ble_crc_attempt`. It does not change hypothesis counts, search ranges, thresholds, CRC, or other profiles. Development and validation here were Windows-only; no ARM measurement is claimed.

## Implementation and equivalence

`m3_workspace_t` now owns a `double` `ble_phase_delta` array of `max_samples` entries. It is allocated and released once per workspace, included in `m3_workspace_bytes`, and regenerated after signal smoothing for each `m3_remoteid_ble_synchronize` call that enters recovery. This includes each of RemoteID028's nine residual-CFO retries, whose rotated IQ differs. For `lag = max(1, sps/4)`, each valid sample stores exactly the original double-precision `atan2((double)a.re*b.im-(double)a.im*b.re, (double)a.re*b.re+(double)a.im*b.im)/(double)lag` expression.

Each hypothesis retains the original `low < 0 || high + lag >= count` rejection and adds cached doubles to `phase_sum` in the same inclusive `low`-to-`high` order. There is no prefix sum or approximation. IQ, `lag`, expression, precision, sum order, soft-bit calculation, slope/intercept, bit decisions, dewhitening, CRC, and hypothesis order are unchanged. The only eliminated work is reevaluation of the same sample's `atan2` within one synchronization call.

The added workspace allocation is `917,504 × 8 = 7,340,032` bytes (7 MiB), independent of candidate length. Reported workspace size rises from 29.06 to 36.06 MiB. There is no per-frame or per-hypothesis allocation. The allocation/free counters each rise from 15 to 16.

## Theoretical `atan2` work

For these 1 Msymbol/s samples, `sps = 31`, `lag = 7`, and the three half-windows are 7, 10, and 13 (15, 21, or 27 samples per bit). Each complete hypothesis tests 376 bits. The old count cannot be recovered exactly from existing counters because some hypotheses exit early on bounds or length. The CRC-check count supplies a conservative lower bound of complete 376-bit hypotheses; the hypothesis count times 376 × 27 supplies an upper bound. A 21-sample average is an illustrative full-window estimate, not an observed count.

| Case | Old `atan2` bound; illustrative estimate | New cache calls | Estimated reduction |
| --- | ---: | ---: | ---: |
| RemoteID005 | 16,215,000–59,165,856; ~46,017,888 | 343,545 (one call) | ~99.25% by estimate |
| RemoteID028 | 164,665,440–613,992,960; ~477,550,080 | 1,877,790 (ten calls) | ~99.61% by estimate |

Even against the conservative lower bound, the call reduction is at least 97.88% for RemoteID005 and 98.86% for RemoteID028. The ten RemoteID028 cache generations are necessary because the initial call and nine CFO trials have different IQ.

## Windows validation and profiling

Baseline: clean `db56f5e` build at `build-pc/windows/a3-baseline/`. Optimized: independent clean Release, profiling-ON build at `build-pc/windows/a3-final/`. CTest passed **1/1**. Across DroneID013, RemoteID005, Autel001, DJI018, and RemoteID028, all **30/30 formal CSV SHA-256 hashes** match the A3 baseline. RemoteID005 remains four CRC-valid PDUs and parses `UAS000185`, altitude 131.0 m, speed 2.25 m/s, heading 117°. RemoteID028 remains `crc_failed`, zero bytes. BLE hypotheses/CRC checks remain 5,828/2,875 and 60,480/29,196 respectively; RemoteID028 still makes nine residual-CFO trials. All other algorithm operation counters match in all five cases; only `CALLOC_CALLS` and `FREE_CALLS` change 15→16.

RemoteID005 numbers below are medians of three Windows runs per build; RemoteID028 uses one run per build. Times are milliseconds.

| Case and stage | Before | After | Reduction | Speedup |
| --- | ---: | ---: | ---: | ---: |
| RemoteID005 `M3_TOTAL` | 1,463.153 | 403.210 | 72.44% | 3.63× |
| RemoteID005 `BLE_SYNC` | 1,208.945 | 145.135 | 87.99% | 8.33× |
| RemoteID005 `BLE_LOW_SNR_RECOVERY` | 1,091.548 | 26.252 | 97.59% | 41.58× |
| RemoteID028 `M3_TOTAL` | 12,690.362 | 1,022.519 | 91.94% | 12.41× |
| RemoteID028 `BLE_SYNC` | 12,510.052 | 848.155 | 93.22% | 14.75× |
| RemoteID028 `BLE_LOW_SNR_RECOVERY` | 11,849.248 | 224.894 | 98.10% | 52.69× |

These are Windows profiling observations, not an ARM speedup prediction. ARM impact and memory suitability require the later consolidated board deployment and measurement.
