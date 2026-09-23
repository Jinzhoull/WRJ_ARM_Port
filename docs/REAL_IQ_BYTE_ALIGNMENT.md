# Real-IQ Byte Alignment: C Module4 vs MATLAB Module4

Date: 2026-09-23. Reference is **only** `records.iqRecoveredBytes` from
`module4_byte_observations.mat`, paired by `candidateId` and unique
`physicalFrameIndex` with the saved Module3 `alignedFrames`. The MATLAB
`records.bytes` Synthetic Harness channel is never read. The C `m4_frame_probe`
receives those same float32 complex IQ samples over stdin and runs the normal
Module4 profile decoder. This isolates Module4 from C/MATLAB Module3 frame-grid
differences. Full per-frame count, first 64 bytes, best byte/bit offsets,
agreement, longest matching run and mismatch positions are written by the
reproduction command below to `build-pc/windows/alignment/frame_alignment.csv`.

| Case / profile | Source | Physical frames | C/MATLAB bytes per frame | Exact byte / bit agreement | Exact frames | Best offset on exact frames (byte/bit) | 180°-invariant byte agreement | Header / checksum | Verdict |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| DJI Wideband 018 / OFDM CP QPSK | REAL_IQ | 20 | 96/96 | 85.23% / 93.22% | 17/20 | 0/0 | 100% | Vendor header unknown; CRC16 candidate unverified | WARN: 3 unresolved polarity choices |
| Autel Wideband 001 / OFDM CP QPSK | REAL_IQ | 20 | 96/96 | 95.16% / 97.76% | 19/20 | 0/0 | 100% | Vendor header unknown; CRC16 candidate unverified | PASS, with 1 polarity warning |
| DroneID 013 / OFDM CP QPSK | REAL_IQ | 5 | 96/96 | 100% / 100% | 5/5 | 0/0 | 100% | Frame extent only; CRC16 candidate unverified | PASS |
| DroneID 022 / OFDM CP QPSK | REAL_IQ | 6 | 96/96 | 99.83% / 99.98% | 5/6 | 0/0 | 99.83% | Frame extent only; CRC16 candidate unverified | PASS |

The best byte and bit offsets are computed independently within bounded
windows; the CSV retains each frame's values, the first 64 bytes from each
receiver, longest matching byte run, and mismatch positions. The nonzero
best offsets on opposite-polarity Wideband frames are incidental short-match
alignments, not evidence of an actual byte slip. All four non-exact Wideband
frames are byte-for-byte complements (`XOR FF`) of the MATLAB IQ-recovered
stream. They are not random errors or FEC failures. A QPSK constellation has
an intrinsic 180° sign ambiguity without a known pilot/header/CRC. MATLAB's
four-rotation score is mathematically tied for rotations 0/2 or 1/3; tiny
floating-point differences can select opposite polarities. The C receiver
uses a deterministic lowest-rotation tie rule. It does **not** select a
rotation by consulting MATLAB bytes or simulation truth. Until a verifiable
frame header or checksum resolves polarity, these frames remain `partial_parse`.

## Corrections made during alignment

1. Applied the MATLAB receiver's per-frame DC removal and RMS normalization.
2. Fixed an in-place complex phase-rotation aliasing bug that overwrote the
   real component before computing the imaginary component.
3. Matched FFT direction, `fftshift` order, DC/guard mapping, active-carrier
   thresholding, fourth-moment phase correction and MSB bit packing.
4. Evaluated full-symbol soft quality and four QPSK rotations before selecting
   the bounded 96-byte observation; retained separate per-physical-frame
   outputs instead of concatenating unrelated frames.
5. Kept an IQ-derived generic timing/QPSK hypothesis for frames where MATLAB
   does not select its OFDM hypothesis, including the DroneID samples.

## Evidence boundary and remaining gap

Agreement with MATLAB `iqRecoveredBytes` verifies that the C implementation
reproduces the MATLAB **receiver's** real-IQ byte observations. It does not
prove those observations are vendor protocol bytes. The current MATLAB Module4
does not implement private DJI/Autel framing, pilot equalization, interleaving,
FEC or a validated checksum on these recovered observations. The Module1/2
generator explicitly labels DJI/Autel payloads as engineering proxies, not
replicas of vendor protocols. Consequently no vendor header, checksum or
field meaning is asserted here. Wideband/DroneID remain `partial_parse` in C.

C Module3 chooses a different DroneID frame grid from the formal MATLAB
Module3 result. Same-frame Module4 agreement does not establish end-to-end
DroneID byte equivalence for C Module3 output; end-to-end ARM measurements are
reported separately in `ARM_VALIDATION.md`.

## Other end-to-end cases

| Case / profile | Byte source and count | Byte agreement / offset | Header and integrity | Field result | Verdict |
|---|---|---|---|---|---|
| RemoteID 005 / BLE GFSK | REAL_IQ; 39 bytes in complete message, 4 distinct PDUs | No per-frame MATLAB byte reference; compare decoded values | Advertising AA and CRC24 valid for all 4 PDUs | Basic ID and Location complete; UAS000185, altitude 131.0 m, speed 2.25 m/s, heading 117° | PASS |
| Unknown 005 / blind differential phase | REAL_IQ; 248 candidate bytes across 8 observations | No validated MATLAB byte reference or meaningful offset | No verified header/checksum | 31-byte frame extent and unverified payload region; semantic UNKNOWN | WARN: structural `partial_parse` only |
| RemoteID 028 / low-SNR BLE | NONE; 0 bytes | Not applicable | No CRC-valid upstream PDU | `upstream_crc_candidate_missing` | Known FAIL; explicitly out of this Module4 scope |

The former Wideband and DroneID output was a single unvalidated 256/131-byte
candidate stream. The corrected result uses 96 IQ-derived bytes per physical
frame and the exact comparisons above. Unknown remains intentionally
conservative: no ID, location, or speed is inferred without evidence.

Reproduce from repository root:

```powershell
python tools/validate_real_iq_bytes.py --observations ../results_wrj_m12_unknown_58g_final/07_MODULE4_FIELD_CLUSTERING_SEMANTIC_INFERENCE/03_BYTE_OBSERVATIONS/module4_byte_observations.mat --aligned-dir ../results_wrj_m12_unknown_58g_final/06_MODULE3_FREQUENCY_FRAME_SYNC/02_ALIGNED_FRAMES --probe build-pc/windows/m4_frame_probe.exe --out build-pc/windows/alignment/frame_alignment.csv --candidate sim_dji_wideband_018_M001 --candidate sim_autel_wideband_001_M001 --candidate sim_dji_droneid_013_M001 --candidate sim_dji_droneid_022_M001
```
