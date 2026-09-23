# Module4 C Port: Representative Validation

Date: 2026-09-23. The cases below use existing Module1/2 handoff rows and
their saved candidate CF32 IQ; no MATLAB code or formal result was modified.
PC timing is end-to-end process time (IQ load + Module3 + Module4 + CSV), not
isolated Module4 latency. Build: C99 MinGW Release with warning flags enabled;
`ctest` passes CRC24, whitening, bit packing, QPSK and field-parser checks.

| Candidate | C result | Real-IQ bytes | CRC | Packets | Fields | End-to-end PC time |
|---|---|---:|---|---:|---:|---:|
| RemoteID 005 | complete | 39 | CRC24 PASS | 4 unique | 7 | 1.51 s |
| RemoteID 028 | byte recovery failed | 0 | no upstream CRC candidate | 0 | 0 | 11.10 s |
| DJI Wideband 018 | partial | 96 per frame | CRC16 candidate not verified | 20 unverified frame observations | 2 structural | ~2.0 s |
| Autel Wideband 001 | partial | 96 per frame | CRC16 candidate not verified | 20 unverified frame observations | 2 structural | ~1.9 s |
| DroneID 013 | partial | 96 per frame | CRC16 candidate not verified | 9 C-Module3 frame observations | 2 structural | ~0.34 s |
| DroneID 022 | partial | 96 per frame | CRC16 candidate not verified | 14 C-Module3 frame observations | 2 structural | ~0.49 s |
| Unknown 005 | partial | 248 | not verified | 8 unverified frame bitstreams | 2 structural | 1.47 s |

RemoteID 005 values recovered from CRC-valid PDU bytes: UAS ID `UAS000185`,
latitude `31.2260243`, longitude `121.4682469`, altitude `131.0 m`, speed
`2.25 m/s`, heading `117°`, and operator ID `OP01185`. These agree with the
MATLAB `candidate_iq` decode in the formal RemoteID table. The C PDU count is
four **distinct** CRC-valid packets; MATLAB has a larger count including
repeated/recovered broadcasts, so the denominators differ.

Wideband and DroneID now save separate bounded 96-byte observations for each
Module3 frame. On the **same aligned IQ**, their C bytes match MATLAB's
`iqRecoveredBytes` as described in `REAL_IQ_BYTE_ALIGNMENT.md`: Autel 95.16%,
DroneID 013 100%, DroneID 022 99.83%, DJI 85.23% exact and 100% after the
unresolved 180° QPSK polarity. This is receiver-byte parity, **not** verified
vendor payload decoding. No private-protocol framing, pilot equalization,
interleaver or FEC is available in the MATLAB Module4 reference. Unknown's
31-byte frame extents all vary in the current unverified bit hypothesis, so
only a frame extent and an unverified payload region are emitted with semantic
`UNKNOWN`. None of these `partial_parse` cases count as full protocol parses.

Workspace: Module3 allocated capacity remains 29.06 MiB; Module4 borrows its
complex filter, metric, scratch and FFT buffers (`incremental_bytes=0`). Small
fixed stack/result arrays add tens of KiB, with no per-frame malloc/free.
RemoteID 028's ~11 s path is the known low-SNR Module3 recovery exception;
Module4 short-circuits after reading its zero-CRC diagnostic.

Reproduce from the repository root (replace `ID` with one of the case basenames). The smoke output path below is under the build directory so it does not overwrite the archived validation CSVs:

```powershell
cmake --build build-pc/windows --config Release
ctest --test-dir build-pc\windows -C Release --output-on-failure
build-pc\windows\wrj_arm_port.exe --handoff data\module12_to_module3_handoff.csv --candidate ID --iq data\cases\ID.cf32 --out build-pc\windows\smoke-results\ID
python tools/validate_module4.py --results results/pc/module4_regression/cases --matlab-remoteid ..\results_wrj_m12_unknown_58g_final\07_MODULE4_FIELD_CLUSTERING_SEMANTIC_INFERENCE\01_TABLES\module4_remoteid_parsed_information.csv
```

Next validation: analyze ARM performance hotspots; resolve Wideband polarity
using independent pilots/header/CRC if available; test C Module3's different
DroneID frame grid; add truly independent byte/PDU references and multiple-SNR
regression. AArch64 remains a separate unvalidated target. Do not compare C
bytes against MATLAB synthetic harness records.
