"""Run acquisition-only C M3 probes; Golden files enter this comparator only."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import subprocess
import time
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
TOLERANCES = {"CFOAbsHz": 1.0, "CFORel": 1e-6, "SFOAbsPpm": 0.1,
              "frameStartSamples": 0, "frameConfidenceAbs": 1e-4,
              "IQRMSE": 1e-4, "IQMaxError": 1e-3, "IQMinCorrelation": 0.999999}

def read_csv(path):
    with path.open(encoding="utf-8-sig", newline="") as file:
        return list(csv.DictReader(file))

def read_iq(path):
    values = np.fromfile(path, dtype="<f4").reshape(-1, 2).astype(np.float64)
    return values[:, 0] + 1j*values[:, 1]

def metrics(left, right):
    if left.shape != right.shape:
        raise ValueError("IQ sample count differs")
    difference = left-right
    denominator = np.linalg.norm(left)*np.linalg.norm(right)
    correlation = float(abs(np.vdot(left, right))/max(denominator, 1e-30))
    return {"RMSE":float(np.sqrt(np.mean(abs(difference)**2))),
            "MaxError":float(np.max(abs(difference))), "Correlation":correlation}

def write_csv(path, rows):
    with path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, list(rows[0]))
        writer.writeheader(); writer.writerows(rows)

def independent_cached_pdu_check(rows, candidate, source):
    for row in rows:
        data = bytes.fromhex(row["bytesHex"])
        suffix = row["receivedCRCBits"]
        if (row["candidate"] != candidate or row["source"] != source or len(data) != 39 or
                len(row["rawBits"]) != 336 or len(suffix) != 24 or
                row["rawBits"][312:] != suffix or any(bit not in "01" for bit in row["rawBits"])):
            return False
        crc = 0x555555
        for byte in data:
            for bit in range(8):
                feedback = ((crc >> 23) & 1) ^ ((byte >> bit) & 1)
                crc = (crc << 1) & 0xffffff
                if feedback:
                    crc ^= 0x65b
        if crc != int(suffix, 2) or crc != int(row["receivedCRC"]):
            return False
    return True

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--reuse-probes", action="store_true", help="Compare existing runs without rerunning receiver")
    args = parser.parse_args()
    output = ROOT/"results_c_validation/m3_core"
    inputs = output/"inputs"
    golden = output/"golden_comparator_only"
    summary = read_csv(golden/"summary.csv")
    golden_frames = read_csv(golden/"frames.csv")
    formal = {row["Signal"]:row for row in read_csv(ROOT.parent/"results/merged_end_to_end_cases.csv")}
    comparisons, frontend_comparisons, runs, manifests = [], [], [], []
    previous_runs = {(row["candidate"],row["mode"]):row for row in read_csv(output/"host_runtime.csv")} if args.reuse_probes else {}
    for reference in summary:
        candidate = reference["candidate"]
        iq_path = inputs/(candidate+".cf32")
        manifests.append({"candidate":candidate,"samples":iq_path.stat().st_size//8,
                          "bytes":iq_path.stat().st_size,"sha256":hashlib.sha256(iq_path.read_bytes()).hexdigest()})
        manifests[-1]["formalInputHashMatches"] = manifests[-1]["sha256"].upper()==formal[candidate]["InputSHA256"].upper()
        if not manifests[-1]["formalInputHashMatches"]:
            raise ValueError("M2 acquisition IQ differs from frozen formal input: "+candidate)
        for mode in ("frontend", "fast"):
            target = output/("c_"+mode)/candidate
            target.mkdir(parents=True, exist_ok=True)
            command = [str(args.executable.resolve()),mode,str(inputs/"handoff.csv"),
                       candidate,str(iq_path),str(target)]
            assert all("golden_comparator_only" not in arg for arg in command)
            if args.reuse_probes:
                previous = previous_runs[candidate,mode]
                code, runtime = int(previous["exitCode"]), float(previous["wallSeconds"])
                log = (target/"run.log").read_text(encoding="utf-8")
            else:
                start = time.perf_counter()
                try:
                    completed = subprocess.run(command,capture_output=True,text=True,timeout=180)
                    code, log = completed.returncode, completed.stdout+completed.stderr
                except subprocess.TimeoutExpired as exc:
                    code, log = -1, str(exc)
                runtime = time.perf_counter()-start
                (target/"run.log").write_text(log,encoding="utf-8")
            runs.append({"candidate":candidate,"mode":mode,"exitCode":code,"wallSeconds":runtime})
            if code != 0:
                raise RuntimeError(f"M3 {mode} probe failed for {candidate}: exit={code} {log}")
            if mode == "frontend":
                for name in ("preprocessed_capture","raw_baseband","preprocessed_baseband"):
                    measured = metrics(read_iq(golden/(candidate+"."+name+".cf32")),read_iq(target/(name+".cf32")))
                    passed = (code==0 and measured["RMSE"]<=TOLERANCES["IQRMSE"] and
                              measured["MaxError"]<=TOLERANCES["IQMaxError"] and
                              measured["Correlation"]>=TOLERANCES["IQMinCorrelation"])
                    frontend_comparisons.append({"candidate":candidate,"stage":name,**measured,"passed":passed})
                continue
            if code != 0:
                raise RuntimeError(f"M3 probe failed for {candidate}: {log}")
            received = read_csv(target/"summary.csv")[0]
            cached_pdus = read_csv(target/"cached_pdus.csv")
            cache_valid = independent_cached_pdu_check(cached_pdus, candidate, formal[candidate]["SourceFile"])
            frames = read_csv(target/"frames.csv")
            expected = [row for row in golden_frames if row["candidate"]==candidate]
            starts_exact = len(frames)==len(expected) and all(int(a["start0"])==int(b["start0"]) for a,b in zip(frames,expected))
            confidence_error = max((abs(float(a["confidence"])-float(b["confidence"])) for a,b in zip(frames,expected)),default=float("nan"))
            start_error = max((abs(int(a["start0"])-int(b["start0"])) for a,b in zip(frames,expected)),default=float("nan"))
            cfo_error = abs(float(received["CFOHz"])-float(reference["CFOHz"]))
            c_sfo, m_sfo = float(received["SfoPpm"]), float(reference["SfoPpm"])
            sfo_nan_matched = math.isnan(c_sfo) and math.isnan(m_sfo)
            sfo_error = 0.0 if sfo_nan_matched else abs(c_sfo-m_sfo)
            iq_metrics = metrics(read_iq(golden/(candidate+".compensated.cf32")),read_iq(target/"compensated.cf32"))
            row = {"candidate":candidate,"MATLABStatus":reference["status"],"CStatus":received["status"],
                   "statusExact":received["status"]==reference["status"],
                   "syncAcceptedExact":int(received["syncAccepted"])==int(reference["syncAccepted"]),
                   "MATLABProfile":reference["profile"],"CProfile":received["profile"],
                   "profileExact":received["profile"]==reference["profile"],
                   "CFOErrorHz":cfo_error,"SFOErrorPpm":sfo_error,"SFONaNMatched":sfo_nan_matched,
                   "MATLABFrames":len(expected),"CFrames":len(frames),"startsExact":starts_exact,
                   "ordinalStartMaxErrorSamples":start_error,"frameConfidenceMaxError":confidence_error,**iq_metrics}
            row["cachedPDUs"] = len(cached_pdus)
            row["cachedOriginalCRCIndependent"] = cache_valid
            row["sourceFingerprint"] = received["sourceFingerprint"]
            row["passed"] = (row["statusExact"] and row["syncAcceptedExact"] and row["profileExact"] and starts_exact
                and confidence_error<=TOLERANCES["frameConfidenceAbs"]
                and cfo_error<=TOLERANCES["CFOAbsHz"]+TOLERANCES["CFORel"]*abs(float(reference["CFOHz"]))
                and sfo_error<=TOLERANCES["SFOAbsPpm"] and iq_metrics["RMSE"]<=TOLERANCES["IQRMSE"]
                and iq_metrics["MaxError"]<=TOLERANCES["IQMaxError"] and iq_metrics["Correlation"]>=TOLERANCES["IQMinCorrelation"] and cache_valid)
            checks = [("STATUS",row["statusExact"] and row["syncAcceptedExact"]),
                      ("PROFILE",row["profileExact"]),
                      ("CFO",cfo_error<=TOLERANCES["CFOAbsHz"]+TOLERANCES["CFORel"]*abs(float(reference["CFOHz"]))),
                      ("SFO",sfo_error<=TOLERANCES["SFOAbsPpm"]),
                      ("FRAME_START_COUNT",starts_exact),
                      ("FRAME_CONFIDENCE",confidence_error<=TOLERANCES["frameConfidenceAbs"]),
                      ("COMPENSATED_IQ",iq_metrics["RMSE"]<=TOLERANCES["IQRMSE"] and
                       iq_metrics["MaxError"]<=TOLERANCES["IQMaxError"] and iq_metrics["Correlation"]>=TOLERANCES["IQMinCorrelation"])]
            row["firstParityMismatch"] = next((name for name,passed in checks if not passed),"NONE")
            row["mismatchChecks"] = "|".join(name for name,passed in checks if not passed)
            comparisons.append(row)
            print(f"{candidate}: frontend/FAST executed; full M3 parity={row['passed']} CFOerror={cfo_error:.3f}Hz",flush=True)
    write_csv(output/"frontend_parity.csv",frontend_comparisons)
    write_csv(output/"module3_fast_parity.csv",comparisons)
    write_csv(output/"host_runtime.csv",runs)
    write_csv(output/"input_sha256.csv",manifests)
    report = {"coreCases":len(summary),"frontendComparisonsPassed":sum(r["passed"] for r in frontend_comparisons),
              "frontendComparisons":len(frontend_comparisons),"module3FastParityPassed":sum(r["passed"] for r in comparisons),
              "hostWallSeconds":sum(r["wallSeconds"] for r in runs),"tolerances":TOLERANCES,
              "algorithmInput":"M2 capture-domain CF32 + whitelisted acquisition handoff only",
              "M3Deep":"FUNCTIONAL_RECEIVED_IQ_RETRY; VALIDATED_IN_HOST_CHAIN","ARMExecuted":False,
              "probeSource":"REUSED_LAST_C_M3_RUN" if args.reuse_probes else "FRESH_C_M3_RUN"}
    report["cachedPDUs"] = sum(row["cachedPDUs"] for row in comparisons)
    report["cachedOriginalCRCIndependent"] = all(row["cachedOriginalCRCIndependent"] for row in comparisons)
    report["sourceFingerprints"] = sorted({row["sourceFingerprint"] for row in comparisons})
    report["probeExecutableSHA256"] = hashlib.sha256(args.executable.read_bytes()).hexdigest()
    (output/"validation_summary.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(json.dumps(report,indent=2))
    if report["module3FastParityPassed"] != 12 or report["frontendComparisonsPassed"] != 36:
        raise SystemExit(1)

if __name__ == "__main__":
    main()
