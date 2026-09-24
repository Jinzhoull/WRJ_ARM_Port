# Module3 A4 Optimization: BLE Whitening Reuse and Invalid-Length Exit

## Scope and baseline

A4 is the last planned Module3 optimization in this phase. It changes only `src/module3/profiles.c`; Module4 and the workspace allocation are untouched. The A3 baseline is the verified Windows Release, profiling-ON `build-pc/windows/a3-final/` at commit `1aac9cc`. The new clean Windows Release, profiling-ON build is `build-pc/windows/a4-final/`. No Ubuntu, ARM cross-build, or board run was performed.

## Changes and equivalence

Previously, `m3_ble_crc_attempt` computed all 376 soft bits, performed the 40-bit sync regression, recovered 336 raw data bits, generated the channel-38 whitening LFSR over all 336 bits, and only then rejected a BLE length outside 6–37. A4 makes two bounded changes:

1. `m3_remoteid_ble_synchronize` generates a 336-bit whitening mask once per call, using the existing `m3_ble_dewhiten` LFSR on zeroes. Its first 120 bits are identical to the prior 120-bit mask because the same initial state and sequential recurrence are used. The same mask supplies `known_signs`. Each CRC attempt receives it; the attempt no longer runs an LFSR. Valid-length hypotheses XOR all 336 raw bits with the mask in ascending bit order.
2. Each attempt first calculates the unchanged soft-bit formula for 40 sync and 16 data bits. It performs the unchanged sync regression and raw-bit decision for the first 16 data bits, then checks header bits 8–15 with `raw ^ whitening_mask`. An invalid length returns immediately. A valid length still computes the remaining 320 soft bits, recovers all remaining raw bits, dewhitens all 336 bits, and executes the original PDU/CRC logic.

For both soft-bit loops, the original `low < 0 || high + lag >= count` bounds check and inclusive low-to-high `phase_sum += phase_delta[sample]` order are preserved. Earlier length rejection is semantically equivalent: an invalid length could not pass the original later length check. No hypothesis is skipped, and no search range, threshold, BLE phase, residual-CFO candidate, CRC rule, or success break changes. The profiler schema is unchanged.

The whitening mask grows from 120 to 336 bytes, adding **216 bytes of local stack** per synchronization call. No new heap buffer is added; the A3 workspace remains 36.06 MiB, with 16 `calloc` and 16 `free` calls.

## Work eliminated

The old per-attempt 336-bit whitening LFSR executed for every attempt reaching raw-bit recovery. Existing counters do not isolate that number. It is bounded below by CRC checks and above by hypotheses: **2,875–5,828** calls for RemoteID005 and **29,196–60,480** for RemoteID028. A4 generates the mask once per synchronization call: one call for RemoteID005 and ten for RemoteID028 (initial plus nine residual-CFO trials). Thus at least **2,874** and **29,186** full LFSR passes, respectively, are eliminated; the exact count is not claimed.

Each hypothesis with an invalid decoded length can stop after **56 instead of 376 soft bits**, avoiding 320 data-bit phase-window sums, or **85.1%** of that hypothesis's maximum soft-bit work. The existing profiler does not count invalid-length hypotheses, so no aggregate soft-bit saving is asserted. Valid-length hypotheses retain the full 376-bit evaluation.

## Correctness and Windows profiling

CTest passed **1/1**. Across DroneID013, RemoteID005, Autel001, DJI018, and RemoteID028, all **30/30 formal CSV SHA-256 hashes** match A3 (six Module3/4 CSVs per case). RemoteID005 alone matches **6/6** and remains four CRC-valid PDUs, `UAS000185`, altitude 131.0 m, speed 2.25 m/s, heading 117°. RemoteID028 alone matches **6/6**, remains `crc_failed` with zero bytes, and still makes nine residual-CFO trials. Hypotheses/CRC checks remain **5,828/2,875** and **60,480/29,196**, respectively. All **85/85 operation-counter rows** match A3, including `CALLOC_CALLS`/`FREE_CALLS` at 16/16.

RemoteID005 times are medians of three Windows runs per build. RemoteID028 uses one run per build; its small whole-Module3 difference should not be overinterpreted. Times are milliseconds.

| Case and stage | A3 | A4 | Reduction |
| --- | ---: | ---: | ---: |
| RemoteID005 `M3_TOTAL` | 403.210 | 388.635 | 3.61% |
| RemoteID005 `BLE_SYNC` | 145.135 | 134.403 | 7.39% |
| RemoteID005 `BLE_LOW_SNR_RECOVERY` | 26.252 | 18.369 | 30.03% |
| RemoteID028 `M3_TOTAL` | 1,022.519 | 1,014.881 | 0.75% |
| RemoteID028 `BLE_SYNC` | 848.155 | 830.367 | 2.10% |
| RemoteID028 `BLE_LOW_SNR_RECOVERY` | 224.894 | 153.869 | 31.58% |

These are Windows measurements only. The cumulative A1–A4 improvement and ARM memory/performance impact await one final Ubuntu/ARM acceptance run; no ARM speedup is inferred here.
