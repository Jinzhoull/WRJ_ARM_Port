"""Read-only CFO diagnostics. Golden data never enters the C receiver."""
import csv
import json
import math
import numpy as np
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "results_c_validation/m3_core"
CASES = ("sim_dji_control_017_M001", "sim_unknown_uav_005_M003", "sim_autel_wideband_012_M001")


def read(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def close(a, b):
    return abs(a-b) <= 1.0 + 1e-6*abs(b)


rows = []
for candidate in CASES:
    golden = json.loads((ROOT / "stage_probes_comparator_only" / (candidate + ".json")).read_text(encoding="utf-8-sig"))
    upstream = read(ROOT / "inputs/handoff.csv")
    fs = float(next(row["sampleRateHz"] for row in upstream if row["candidateId"] == candidate))
    raw_cp = read(ROOT / "stage_probes_comparator_only" / (candidate + ".cp.csv"))
    for actual in read(ROOT / "c_fast" / candidate / "cfo_stages.csv"):
        stage = int(actual["stage"])
        increment = golden["increments"][stage-1]
        derived = float(actual["CPDerivedHz"])
        executed = int(actual["NFFT"]) > 0
        correlation_angle = math.atan2(float(actual["correlationIm"]), float(actual["correlationRe"]))
        angle_derived = correlation_angle*fs/(2*math.pi*int(actual["NFFT"])) if executed else 0.0
        golden_fine = golden["combinedCFO"] - golden["spectral"]
        selected = [row for row in raw_cp if int(row["stage"]) == stage]
        cut = np.quantile([float(row["coherence"]) for row in selected], .25)
        fused = sum(float(row["coherence"])**2 * complex(float(row["re"]), float(row["im"])) /
                    (abs(complex(float(row["re"]), float(row["im"])))+1e-20)
                    for row in selected if float(row["coherence"]) >= cut)
        correlation_error = abs(complex(float(actual["correlationRe"]), float(actual["correlationIm"]))-fused) if executed else None
        checks = {
            "numerologyExact": not executed or (int(actual["NFFT"]) == golden["nfft"] and int(actual["CPLength"]) == golden["cp"]),
            "spectralCoarseExact": float(actual["spectralCoarseHz"]) == golden["spectral"],
            "CPDerivedWithinOriginalTolerance": close(derived, increment),
            "CPPeakCountExact": not executed or int(actual["CPPeaks"]) == len(selected),
            "angleSignAndScaleConsistent": abs(derived-angle_derived) < 1e-8,
            "stage1WithinOriginalTolerance": close(float(actual["stage1Hz"]), golden["increments"][0]),
            "stage2WithinOriginalTolerance": close(float(actual["stage2Hz"]), golden["increments"][1]),
            "combinedWithinOriginalTolerance": close(float(actual["combinedHz"]), golden_fine),
            "appliedWithinOriginalTolerance": close(float(actual["appliedHz"]), golden["combinedCFO"]),
            "residualWithinOriginalTolerance": close(float(actual["residualHz"]), golden["increments"][2]),
            "phaseOriginExact": int(actual["phaseOrigin"]) == golden["phaseOrigin"],
        }
        rows.append({**actual, "CPStageExecuted": executed, "goldenCPDerivedHz": increment, "CPDerivedErrorHz": abs(derived-increment),
                     "goldenCPPeaks": len(selected), "goldenFusedCorrelationRe": fused.real,
                     "goldenFusedCorrelationIm": fused.imag, "fusedCorrelationAbsError": correlation_error,
                     "goldenCombinedFineHz": golden_fine,
                     "goldenCombinedHz": golden["combinedCFO"],
                     "combinedErrorHz": abs(float(actual["combinedHz"])-golden_fine),
                     **checks, "passed": all(checks.values())})
with (ROOT / "cfo_stage_parity.csv").open("w", encoding="utf-8", newline="") as stream:
    writer = csv.DictWriter(stream, list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
report = {"CPStageProbes": len(rows), "passed": sum(row["passed"] for row in rows),
          "actuallyExecutedCPStages": sum(row["CPStageExecuted"] for row in rows),
          "maxCPDerivedErrorHz": max(row["CPDerivedErrorHz"] for row in rows),
          "maxCombinedErrorHz": max(row["combinedErrorHz"] for row in rows),
          "correlationDefinition": "C selected CP correlations after coherence-quartile rejection, normalized and coherence-squared weighted; MATLAB raw per-peak correlations retained separately",
          "tolerance": "Unchanged M3 CFO tolerance: abs <= 1Hz + 1e-6*abs(GoldenHz)",
          "phaseOrigin": "n=0", "inputBoundary": "Offline comparator only"}
report["combinedDefinition"] = "combinedHz is the sum of fractional increments; appliedHz additionally includes spectral coarse CFO. Blind/fallback profiles execute one CP CFO stage; absent later stages are zero and not counted as executed."
(ROOT / "cfo_stage_summary.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps(report, indent=2))
if report["passed"] != len(rows):
    raise SystemExit(1)
