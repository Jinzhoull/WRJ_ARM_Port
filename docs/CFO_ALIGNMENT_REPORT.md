# Module3 CFO Alignment and Recovery Report

## Scope and comparison convention

The reports consolidated here compare C Module3 outputs against MATLAB's `totalBasebandCorrectionHz` on the same 11 candidate IQ inputs. Deltas are C minus MATLAB, in Hz. This is a reference-implementation comparison, not a truth-based accuracy score. For non-BLE profiles, `residualCfoHz` is a post-compensation diagnostic in MATLAB while the C output does not yet expose the equivalent measurement; do not add that residual delta to total CFO. The final 11-case run is archived under `build-pc/windows/latest/`.

## Fix history

1. Corrected the initial boxcar smoothing window in `src/module3/cfo.c` to match MATLAB's zero-padded `conv(...,'same')` behavior. This removed a spectrum-center bias; DroneID013 coarse-CFO delta moved from about +50.0 kHz to below 1 kHz.
2. Separated BLE residual correction from fractional CFO output in `src/module3/module3.c`, so the BLE correction is represented and counted once rather than duplicated across fields.
3. Matched the spectral-center parabolic interpolation denominator/limiting convention to MATLAB. This improved consistency for DroneID022 and several other candidates without changing FFT length, sampling rate, averaging, integer-CFO search, or thresholds.
4. Changed BLE residual-CFO candidate ranking to prioritize CRC-valid packet count, then use existing synchronization evidence to break ties. Search candidates and thresholds stayed fixed. RemoteID028 selected a different residual hypothesis but still had no valid CRC packet.
5. Added Control-profile-only FFT band-limiting consistent with the MATLAB passband definition. DJI Control004 fractional-CFO delta improved from +3769.551 Hz to +338.749 Hz. This changed three Control004 Module4 CSVs but kept its parse status `partial`.

Unsuccessful Control re-estimation and BLE CFO-hint experiments were reverted. No sample-specific offsets, truth input, threshold relaxation, or output-field fudging was retained.

## Final 11-case CFO deltas

| Signal | Coarse Δ | Fractional Δ | Residual Δ* | Total Δ | Assessment |
|---|---:|---:|---:|---:|---|
| Autel Control009 | −0.19 | +665.54 | +4551.63 | +665.34 | CP fine estimate remains different |
| Autel WB001 | 0.00 | +0.90 | +64.44 | +0.90 | Hz-level total alignment |
| Autel WB012 | 0.00 | +1.09 | −137.57 | +1.09 | Hz-level total alignment |
| Autel WB015 | −0.25 | −3.63 | +139.92 | −3.96 | Hz-level total alignment |
| DJI Control004 | 0.00 | +338.75 | −7756.05 | +338.75 | Improved; residual field is not comparable |
| DJI DroneID013 | +0.04 | −64.83 | +2272.17 | −64.79 | Within about 65 Hz |
| DJI DroneID022 | +0.06 | +153.23 | −1725.87 | +153.28 | Remaining ~153 Hz difference |
| DJI WB018 | 0.00 | −1.18 | −103.46 | −1.18 | Hz-level total alignment |
| RemoteID005 | +0.06 | 0.00 | 0.00 | +0.06 | Aligned; parsing preserved |
| RemoteID028 | −0.06 | 0.00 | −28446.46 | −28446.52 | BLE recovery remains unresolved |
| Unknown005 | 0.00 | +127.46 | −218.40 | +127.54 | Remaining ~128 Hz difference |

`*` Non-BLE residual deltas are diagnostic-field differences, not additional correction terms. Small rounding differences may occur between the rounded table and the exact `final_comparison.csv` values.

## Regression and remaining limitations

The final Windows Release validation ran all 11 cases through Module3 and Module4; CTest passed 1/1. All profiles, frame lengths/counts, and Module4 statuses were unchanged between the final Control-only comparison runs. Three of 44 Module4 CSVs changed for DJI Control004 (`module4_bytes.csv`, `module4_packets.csv`, and `module4_result.csv`); its status remained `partial`. RemoteID005 retained four CRC24-valid PDUs, UAS ID `UAS000185`, and its decoded location/motion fields. RemoteID028 remained `crc_failed` with zero CRC24-valid PDU in C versus five in MATLAB; it is not a resolved recovery case.

Wideband totals remain within roughly 4 Hz of MATLAB. Other residual gaps are concentrated in Autel Control009 (~665 Hz), DJI Control004 (~339 Hz), DroneID022 (~153 Hz), Unknown005 (~128 Hz), and RemoteID028 (~28.45 kHz). RemoteID028's large difference follows the missing packet-level soft recovery/CRC evidence; the output is not declared successful based on CFO proximity alone. No Git commit was made as part of the reports consolidated here.
