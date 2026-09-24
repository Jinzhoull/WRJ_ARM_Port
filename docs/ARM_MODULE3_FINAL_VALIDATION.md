# WRJ_ARM_Port Module3 A1–A4 final ARMv7 validation

## Revision and build

- Branch/HEAD: `perf/module3-optimized` / `680688737d8d20d133d9e1fa21bfd5ac0236e636` (`6806887`). A4 code commit: `e0930b4`; no merge with `main`.
- Target board: `analog-board`, Linux `armv7l`; ARM hard-float, EABI5, static link.
- Toolchain: `arm-linux-gnueabihf-gcc` 9.4.0 (Ubuntu 20.04 package), Binutils 2.34, CMake 3.22.1.
- Profiling build: Release, `-O2 -DNDEBUG`, `WRJ_ENABLE_PROFILING=ON`, `WRJ_ENABLE_NEON=OFF`, linker `-static`.
- Profiling binary SHA256: `93ddc5b03ed82cfbaa35ba9085f5c338108430b650e97c207de19155b3ed5544`.
- Production build: separate `build-arm-final-release`, Release, `-O2 -DNDEBUG`, profiling OFF, NEON OFF, static.
- Production binary SHA256: `4b9cc366e2bf231a7b966fe5073160f48f50e63c59400fa80fcbd30595221b8a`. Board copy has the same SHA256.
- Previous build tree and beff0f0 profile artifacts were preserved intact as `build-arm-beff0f0-preserved`; no prior profile data was deleted or overwritten.

## Five-case acceptance and performance

Performance comparison uses the same profiling-enabled ARM setup: beff0f0 versus 6806887. Reduction is `(old-new)/old`; speedup is `old/new`.

| Case | Module3 / Module4 result | Old M3_TOTAL | Final M3_TOTAL | Reduction | Speedup | Final Peak RSS |
|---|---|---:|---:|---:|---:|---:|
| DroneID013 | `ok` / `partial`, 96 bytes, 9 packets, CRC not verified | 4,148.519 ms | 3,462.654 ms | 16.53% | 1.198× | 9,956 kB (9.72 MiB) |
| RemoteID005 | `ok` / parsed, 4 CRC24-valid PDUs | 33,053.482 ms | 6,757.771 ms | 79.56% | 4.891× | 15,040 kB (14.69 MiB) |
| Autel Wideband001 | `ok` / `partial`, 96 bytes, 20 observations | 20,867.827 ms | 17,230.596 ms | 17.43% | 1.211× | 20,944 kB (20.45 MiB) |
| DJI Wideband018 | `ok` / `partial`, 96 bytes, 20 observations | 20,496.279 ms | 19,905.757 ms | 2.88% | 1.030× | 24,248 kB (23.68 MiB) |
| RemoteID028 | `ok` / `crc_failed`, 0 bytes | 285,851.969 ms | 18,122.383 ms | 93.66% | 15.773× | 10,840 kB (10.59 MiB) |

RSS is the board process `VmHWM` sampled during each run. Previous beff0f0 logs did not record RSS, so old RSS is unavailable. The printed workspace capacity is 36.06 MiB after A3; the board has 497 MiB total RAM. No OOM, crash, or swap use occurred. After the runs, swap remained 0 and `/` had about 2.7 GiB free.

## Formal CSV equivalence

For each of the five cases, the following six CSVs were SHA/content-compared against both the original `first-run-20260923-o2` ARM results and the beff0f0 profiling-run formal outputs:

`module3_result.csv`, `module3_frames.csv`, `module4_result.csv`, `module4_fields.csv`, `module4_packets.csv`, `module4_bytes.csv`.

Result: **30/30 files match each baseline** (60/60 file comparisons total). No formal CSV differences were found. Profiling CSVs are separate and are not part of this comparison.

## Stage timing changes (beff0f0 → 6806887, ms)

Stage timers may be inclusive/nested; compare each row individually and do not sum overlapping stages.

| Case | Stage | Old | Final |
|---|---|---:|---:|
| DroneID013 | BANDLIMIT_FIR | 1,431.898 | 1,342.679 |
| DroneID013 | INTEGER_CFO | 558.182 | 199.065 |
| DroneID013 | SPECTRUM_ANALYSIS | 1,016.398 | 650.314 |
| DroneID013 | DRONEID_SYNC | 592.014 | 665.126 |
| Autel001 | PROFILE_SELECTION | 6,359.675 | 5,515.183 |
| Autel001 | BANDLIMIT_FIR | 4,312.040 | 4,048.941 |
| Autel001 | INTEGER_CFO | 1,730.831 | 590.084 |
| Autel001 | SPECTRUM_ANALYSIS | 1,381.225 | 800.459 |
| Autel001 | WIDEBAND_SYNC | 4,928.184 | 4,298.141 |
| DJI018 | PROFILE_SELECTION | 5,593.448 | 6,332.527 |
| DJI018 | BANDLIMIT_FIR | 5,151.264 | 4,833.844 |
| DJI018 | INTEGER_CFO | 2,006.510 | 713.499 |
| DJI018 | SPECTRUM_ANALYSIS | 1,357.066 | 829.433 |
| DJI018 | WIDEBAND_SYNC | 4,008.047 | 4,593.636 |
| RemoteID005 | BLE_SYNC | 28,190.338 | 2,536.379 |
| RemoteID005 | BLE_LOW_SNR_RECOVERY | 26,514.042 | 767.798 |
| RemoteID005 | BLE_RESIDUAL_CFO | 0.000 | 0.000 |
| RemoteID005 | BLE_PREAMBLE_AA | 1,676.037 | 1,768.308 |
| RemoteID005 | BLE_PHASE_SEARCH | 758.513 | 771.567 |
| RemoteID028 | BLE_SYNC | 282,327.648 | 15,046.052 |
| RemoteID028 | BLE_LOW_SNR_RECOVERY | 273,425.119 | 6,154.645 |
| RemoteID028 | BLE_RESIDUAL_CFO | 254,768.094 | 14,226.098 |
| RemoteID028 | BLE_PREAMBLE_AA | 8,901.456 | 8,890.292 |
| RemoteID028 | BLE_PHASE_SEARCH | 4,058.261 | 4,065.858 |

In particular, RemoteID028 `BLE_LOW_SNR_RECOVERY` fell from 273,425.119 ms to 6,154.645 ms, and `BLE_RESIDUAL_CFO` from 254,768.094 ms to 14,226.098 ms. Its result remains `crc_failed` with 0 bytes; the optimization did not turn the case into a success.

## Operation counters

Search-related operation counters match beff0f0 for all five cases. Allocation counters are the expected A3 change: `MALLOC/CALLOC/FREE = 0/16/16`, previously `0/15/15`.

| Case | FFT | Correlation calls / evaluations | CFO candidates / integer | Frame candidates |
|---|---:|---:|---:|---:|
| DroneID013 | 96 | 639 / 1,508,415 | 7 / 7 | 202,117 |
| RemoteID005 | 21 | 338,592 / 32,504,832 | 0 / 0 | 340,367 |
| Autel001 | 40 | 7 / 2,530,615 | 7 / 7 | 2,530,615 |
| DJI018 | 40 | 7 / 2,767,863 | 7 / 7 | 2,767,863 |
| RemoteID028 | 12 | 1,828,260 / 175,512,960 | 9 / 0 | 1,838,009 |

| Case | BLE phases / residual candidates | Hypotheses | CRC checks |
|---|---:|---:|---:|
| DroneID013 | 0 / 0 | 0 | 0 |
| RemoteID005 | 31 / 0 | 5,828 | 2,875 |
| Autel001 | 0 / 0 | 0 | 0 |
| DJI018 | 0 / 0 | 0 | 0 |
| RemoteID028 | 310 / 9 | 60,480 | 29,196 |

RemoteID005 still parses UAS ID `UAS000185`, altitude 131.0 m, speed 2.25 m/s, heading 117°. RemoteID028 retains all nine residual-CFO candidates and the expected hypothesis/CRC counts.

## Profiling OFF production smoke

The production Release binary was deployed to `/root/wrj_arm_test/bin/wrj_arm_port` and used for one DroneID013 smoke only. It exited normally with Module3 `ok`, Module4 `partial`, 96 bytes and 9 packets; no profiling CSV was emitted. Production-smoke Peak RSS was 9,952 kB (9.72 MiB). The other four cases were not rerun with profiling OFF.

## Recommendation and working tree

All A1–A4 optimizations are recommended for retention: all 30 formal outputs are byte-identical, search-related counters are unchanged, and each case improved end-to-end M3_TOTAL (from 2.88% to 93.66%). The strongest measured wins are A3/A4 on BLE cases, especially RemoteID028; wideband and DroneID gains are smaller and stage timing has some per-case variation.

No source, Module4, thresholds, search ranges, `-O2`, or NEON setting was changed during this Ubuntu acceptance. No Git add/commit/push was performed. Final Git status is clean.

NEON attribute note: CMake says `WRJ_ENABLE_NEON=OFF`; project object compile flags use `-mfpu=vfpv3-d16`, and no project-owned object has an Advanced SIMD ELF attribute. The final statically linked ELF's aggregate `readelf -A` output nevertheless reports `NEONv1`; the cross sysroot `libc.a` contains NEON-tagged members. This is a static-runtime attribute caveat, not an explicit NEON build of the project. The entire linked executable was not instruction-by-instruction audited.
