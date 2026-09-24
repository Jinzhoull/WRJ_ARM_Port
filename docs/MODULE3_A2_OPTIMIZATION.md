# Module3 A2 Optimization

## Scope and fixed baseline

This cumulative branch starts from A1 commit `31d03c7`. The existing `build-pc/windows/a1-final/` build was confirmed as Release with profiling enabled. Its five cases have all seven expected CSVs, and its 30 formal CSVs still match the `beff0f0` functional baseline byte for byte. `build-pc/windows/a1-final/A2_SHA256_manifest.csv` fixes the A2 comparison hashes. All A2 builds and runs used Windows GNU 16.1; no Ubuntu, ARM cross-build, or board run was performed.

## A2-1 FFT stage twiddle reuse

`src/module3/fft.c:m3_fft_forward_radix2()` now iterates `span → offset → base`. A stage calculates `angle = step * (float)offset`, `cosf(angle)`, and `sinf(angle)` once per `(span, offset)`. The bit reversal, span order, float types, trigonometric functions, and butterfly expressions are unchanged.

For fixed `span`, each pair `(base + offset, base + offset + half)` is unique: offsets partition the two half-spans, and different `base` values address disjoint spans. No butterfly in that stage reads or writes another butterfly's pair, so exchanging the two inner loops preserves every pair's input and result. The five cases produced **30/30 byte-identical formal CSVs** immediately after this change. FFT call counts remained **96, 21, 40, 40, 12**, respectively. Commit: `6775a0f`.

For an N-point radix-2 FFT, the old loop evaluates `cosf` and `sinf` each `(N/2)log2(N)` times; the new loop evaluates each `N−1` times. The observed call counts and code paths imply the following FFT length mix. `FFT_CALLS` is counted until after Module4, so the mix includes its calls to the shared FFT function.

| Case | FFT lengths × calls | Old calls per trig function | New calls per trig function | Reduction |
|---|---|---:|---:|---:|
| DroneID013 | 32768×11, 65536×2, 2048×83 | 4,686,848 | 661,408 | 85.89% |
| RemoteID005 | 32768×19, 65536×2 | 5,718,016 | 753,643 | 86.82% |
| Autel001 | 32768×20, 1024×20 | 5,017,600 | 675,800 | 86.53% |
| DJI018 | 32768×20, 2048×20 | 5,140,480 | 696,280 | 86.45% |
| RemoteID028 | 32768×10, 65536×2 | 3,506,176 | 458,740 | 86.92% |

Across these five runs, this saves a theoretical **20,823,249 `cosf` calls and the same number of `sinf` calls** (86.51% each). Lengths are inferred from the fixed call sites, input sample counts, frame counts, and matching `FFT_CALLS`; the profiler does not have a per-length counter. The reduction is a call-count calculation, not a measured ARM speedup.

## A2-2 Spectral Hann window reuse

`src/module3/cfo.c:m3_estimate_spectral_center()` now constructs the same float Hann coefficient once per bin before the FFT block loop. Every block reads that coefficient and retains the original IQ multiplication, block-energy accumulation, FFT, and power accumulation order. The expression inside `cosf` is unchanged.

The temporary cache is `workspace->spectrum_weight`. `m3_workspace_init()` allocates exactly `spectrum_length` floats there, equal to this function's `fft_size`. The FFT block loop neither reads it for another purpose nor calls code that uses it. The function first repurposes this buffer for actual spectral weights after the block loop and noise estimate, so the cache is dead before that overwrite. No stack VLA or heap allocation was added. The five cases produced **30/30 byte-identical formal CSVs** immediately after this change; FFT call counts were unchanged. Commit: `f524758`.

All five cases use `fft_size=32768`. Old Hann `cosf` calls are `block_count × 32768`; new calls are 32768 per spectral-center invocation.

| Case | Spectrum blocks | Old Hann `cosf` | New Hann `cosf` | Reduction |
|---|---:|---:|---:|---:|
| DroneID013 | 11 | 360,448 | 32,768 | 90.91% |
| RemoteID005 | 19 | 622,592 | 32,768 | 94.74% |
| Autel001 | 20 | 655,360 | 32,768 | 95.00% |
| DJI018 | 20 | 655,360 | 32,768 | 95.00% |
| RemoteID028 | 10 | 327,680 | 32,768 | 90.00% |

The five runs save a theoretical **2,457,600 Hann `cosf` calls** (93.75%).

## Final Windows regression

The independent clean Release, profiling-ON build at `build-pc/windows/a2-final/` passed **CTest 1/1**. Its five cases match the A2 baseline in **30/30 formal CSV SHA-256 hashes**. This includes Module3 CFO, sync and frame fields and all Module4 result and byte files. All **80/80 operation-counter rows** (16 per case) also match, including FFT, correlation, rotation, CFO/BLE hypotheses, CRC, frame-candidate, and allocation counts. Module4 source, search ranges, thresholds, formulas, and build optimization settings were not changed.

For the four shorter cases, each build ran three times and the following times are medians. RemoteID028 was run once per build for correctness and has single-run times. All values are milliseconds.

| Case | M3_TOTAL A1 → A2 | SPECTRUM_ANALYSIS A1 → A2 | COARSE_CFO A1 → A2 |
|---|---:|---:|---:|
| DroneID013 | 253.627 → 241.104 | 67.648 → 59.661 | 70.702 → 62.428 |
| RemoteID005 | 1397.553 → 1383.818 | 89.813 → 81.160 | 95.061 → 85.885 |
| Autel001 | 1842.685 → 1856.420 | 80.778 → 69.886 | 89.404 → 78.479 |
| DJI018 | 1884.857 → 1871.375 | 81.055 → 70.138 | 91.447 → 80.432 |
| RemoteID028 (one run) | 11733.473 → 11393.581 | 59.891 → 54.592 | 62.635 → 57.177 |

Windows profiling indicates the FFT loop exchange gave the larger visible reduction in the spectrum stage. In the single-run checkpoints, `SPECTRUM_ANALYSIS` moved from A1 to FFT-only by −6.94, −8.46, −9.41, −9.63, and −3.10 ms in case order; adding Hann reuse made smaller changes in four cases, while RemoteID005 fluctuated upward in that single run. The three-run final medians show consistent spectrum and coarse-CFO reductions, but the total time for Autel increased slightly. These Windows measurements are a sanity check; no ARM benefit is claimed until the planned final board validation of the completed Module3 optimization phase.
