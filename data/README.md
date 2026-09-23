# Test data

`module12_to_module3_handoff.csv` is the single shared Module1/2 metadata handoff. Each IQ file is a CF32 little-endian complex-float input named by its candidate ID.

The `candidateIqArtifact` column uses portable paths relative to the original MATLAB results root (`results_wrj_m12_unknown_58g_final/05_DATA/module3_candidate_iq/`). Those MATLAB MAT artifacts are not bundled here. The `artifactAvailable` values describe the upstream handoff snapshot. For this C port, use the 11 checked-in files under `data/cases/` and pass the selected CF32 file explicitly with `--iq`.

The current checked-in regression set contains 11 distinct candidates, all used by the existing Module3 comparison report:

- `sim_dji_droneid_013_M001`
- `sim_remoteid_ble_005_M001`
- `sim_autel_wideband_001_M001`
- `sim_dji_wideband_018_M001`
- `sim_remoteid_ble_028_M001`
- `sim_autel_control_009_M005`
- `sim_autel_wideband_012_M001`
- `sim_autel_wideband_015_M001`
- `sim_dji_control_004_M001`
- `sim_dji_droneid_022_M001`
- `sim_unknown_uav_005_M003`

The first five are the representative ARM/PC board cases. The remaining six are retained because they are part of the existing 11-case Module3 regression set. Keep the shared handoff CSV as one file; do not make a per-case copy. These files are unique validated inputs, not temporary conversions.
