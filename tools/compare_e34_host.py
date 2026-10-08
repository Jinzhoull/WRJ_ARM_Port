"""Offline comparator only. Never linked to, or called by, the C receiver."""
import argparse
import csv
import json
import math
import hashlib
import re
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def write(path, rows):
    if not rows:
        return
    columns = list(dict.fromkeys(key for row in rows for key in row))
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, columns)
        writer.writeheader()
        writer.writerows(rows)


def bit_bytes(bits):
    bits = [int(bit) for bit in bits]
    return bytes(sum(bits[8*k+j] << j for j in range(8)) for k in range(len(bits)//8))


def number_equal(first, second):
    a = float("nan") if first is None else float(first)
    b = float("nan") if second is None else float(second)
    return (math.isnan(a) and math.isnan(b)) or math.isclose(a, b, rel_tol=1e-12, abs_tol=1e-10)


def fields_equal(actual, expected):
    if len(actual) != len(expected):
        return False
    for a, b in zip(actual, expected):
        for key in ("semantic", "dataType", "asciiValue", "validationStatus"):
            if a.get(key, "") != b.get(key, ""):
                return False
        if a["rawHex"].upper() != b["rawHex"].upper():
            return False
        if any(int(a[key]) != int(b[key]) for key in ("startByte", "endByte")):
            return False
        if not number_equal(a["numericValue"], b["numericValue"]):
            return False
        if int(a["crcVerified"]) != int(b["crcVerified"]):
            return False
    return True


def golden_evidence(records):
    evidence = defaultdict(dict)
    for record in records:
        cid = record["candidateId"]
        if record["signalCategory"] != "RemoteID_BLE":
            if record["parseComplete"]:
                data = bytes(record["iqRecoveredBytes"])
                evidence[cid][data.hex().upper()] = {
                    "fields": record["protocolFields"], "crcBits": "",
                    "start1": record["receivedFrameStartSample"],
                }
            continue
        for packet in record["remoteIdPacketEvidence"]:
            if not packet["crcOk"] or not packet["found"] or len(packet["verifiedPduBits"]) != 312:
                continue
            data = bit_bytes(packet["verifiedPduBits"])
            address = f"{(data[0] >> 6) & 1}:{data[2:8].hex().upper()}"
            fields = [field for field in record["remoteIdFields"]
                      if field["advertiserAddress"] == address and field["messageCounter"] == data[13]]
            suffix = "".join(str(int(bit)) for bit in packet["rawBits"][312:336])
            evidence[cid][data.hex().upper()] = {
                "fields": fields, "crcBits": suffix, "start1": packet["startSample"],
            }
    return evidence


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--run-name", default="final")
    parser.add_argument("--allow-progress", action="store_true")
    args = parser.parse_args()
    base = ROOT / "results_c_validation/host53"
    gold = base / "golden_comparator_only"
    prefix = (args.run_name + "_") if args.run_name else ""
    summaries = read(base / (prefix + "host_summary.csv"))
    records = json.loads((gold / "final_records.json").read_text(encoding="utf-8-sig"))
    expected = golden_evidence(records)
    fast_expected = golden_evidence(json.loads((gold / "fast_records.json").read_text(encoding="utf-8-sig")))
    cases = {row["Signal"]: row for row in read(gold / "merged_end_to_end_cases.csv")}
    upstream = {row["candidateId"]: row for row in read(gold / "module5_parse_results.csv")}
    truth = defaultdict(list)
    for frame in read(gold / "protocol_frame_truth.csv"):
        truth[frame["sourceFile"]].append(frame)
    frame_rows, parity_rows, protocol_rows = [], [], []
    by_protocol = defaultdict(list)
    for summary in summaries:
        cid = summary["candidate"]
        case = cases[cid]
        directory = base / ("c_" + prefix + "host") / cid
        received = read(directory / "records.csv")
        fields = read(directory / "fields.csv")
        actual_fields = defaultdict(list)
        for field in fields:
            actual_fields[int(field["record"])].append(field)
        actual_bytes = {record["bytesHex"] for record in received}
        expected_bytes = set(expected[cid])
        matching = actual_bytes & expected_bytes
        # The recovery machine merges by append only. FAST evidence is the
        # immutable prefix counted before the machine starts; no record is
        # replaced by a subsequent receiver hypothesis.
        fast_received = received[:int(summary["FastRecords"])]
        fast_bytes = {record["bytesHex"] for record in fast_received}
        fast_exact = (fast_bytes == set(fast_expected[cid]) and
                      len(fast_received) == len(fast_expected[cid]) and
                      all(record["receivedCRCBits"] == fast_expected[cid][record["bytesHex"]]["crcBits"] and
                          fields_equal(actual_fields[int(record["index"])], fast_expected[cid][record["bytesHex"]]["fields"])
                          for record in fast_received if record["bytesHex"] in fast_expected[cid]))
        exact_fields = exact_crc = 0
        for record in received:
            reference = expected[cid].get(record["bytesHex"])
            if reference:
                exact_fields += fields_equal(actual_fields[int(record["index"])], reference["fields"])
                exact_crc += record["receivedCRCBits"] == reference["crcBits"]
        physical, partial, inside = 0, 0, 0
        for frame in truth[case["SourceFile"]]:
            included = (int(frame["validFrame"]) and
                        int(frame["startSample"]) >= int(case["CropStartSample"]) and
                        int(frame["endSample"]) <= int(case["CropEndSample"]))
            overlap = (int(frame["endSample"]) >= int(case["CropStartSample"]) and
                       int(frame["startSample"]) <= int(case["CropEndSample"]))
            matched = False
            for record in received:
                # Offline identification is confined to this comparator.
                # Full receiver bytes must also match the saved Golden evidence.
                if record["bytesHex"] not in matching:
                    continue
                data = bytes.fromhex(record["bytesHex"])
                remote = record["protocol"] == "RemoteID_BLE"
                crc_hex = f"{int(record['receivedCRC']):06X}" if remote else data[-2:].hex().upper()
                payload_hex = data[14:].hex().upper() if remote else data[11:-2].hex().upper()
                sequence = data[13] if remote else data[3]
                if (crc_hex == frame["crcHex"].upper() and sequence == int(frame["sequenceNumber"]) and
                        payload_hex.startswith(frame["payloadDigest"].upper())):
                    matched = True
                    break
            inside += int(bool(included))
            physical += int(included and matched)
            partial += int(not included and overlap and matched)
            frame_rows.append({"candidate": cid, "protocol": case["Protocol"], "frame": frame["frameIndex"],
                               "insideCrop": int(bool(included)), "receivedBytesMatch": int(matched),
                               "completePhysicalRecovered": int(included and matched)})
        strict = int(summary["M5Success"]) * int(upstream[cid]["linkCorrectEvaluation"]) * int(upstream[cid]["protocolCorrectEvaluation"])
        types = {int(record["messageType"]) for record in received if record["protocol"] == "RemoteID_BLE"}
        scope_exact = all(record["candidate"] == cid and record["source"] == case["SourceFile"]
                          and record["byteSource"] == "REAL_IQ" and int(record["originalCRCPresent"]) == 1
                          and int(record["crcValid"]) == 1 and int(record["semanticComplete"]) == 1
                          and record["receivedCRC"] == record["computedCRC"] for record in received)
        retry_calls_exact = all(int(summary[c_key]) == int(upstream[cid][m_key]) for c_key, m_key in
                                (("M3DeepCalls", "M3RetryCalls"), ("M4DeepCalls", "M4RetryCalls"),
                                 ("ProtocolCalls", "ProtocolRetryCalls")))
        gate_exact = (int(summary["M5Success"]) == int(case["M5Status"] == "SUCCESS") and
                      int(summary["M5CRCValid"]) == len(received) and
                      int(summary["M5SemanticComplete"]) == len(received))
        passed = (summary["GoldenFastStatusMatches"] == "True" and summary["GoldenFinalStatusMatches"] == "True" and
                  actual_bytes == expected_bytes and exact_fields == len(received) and exact_crc == len(received) and
                  len(received) == len(expected_bytes) and fast_exact and scope_exact and retry_calls_exact and gate_exact and
                  strict == int(case["StrictEndToEnd"]) and physical == int(case["RecoveredPhysicalFrames"]))
        row = {"candidate": cid, "protocol": case["Protocol"], "generatedFrames": case["GeneratedFrames"],
               "framesInsideCrop": inside, "recoveredPhysical": physical, "partialWaveformDecoded": partial,
               "M3Status": summary["M3Status"], "M3SyncAccepted": summary["InitialM3Accepted"],
               "M4Fast": summary["M4FastComplete"], "M4Final": summary["complete"], "M5Flow": summary["M5Success"],
               "FASTCRCRecords": len(fast_received), "GoldenFASTCRCRecords": len(fast_expected[cid]),
               "FASTBytesCRCFieldsExact": fast_exact,
               "strict": strict, "deepAttempted": summary["DeepAttempted"], "deepRecovered": summary["DeepRecovered"],
               "CRCRecords": len(received), "GoldenCRCRecords": len(expected_bytes), "fields": len(fields),
               "GoldenFields": sum(len(e["fields"]) for e in expected[cid].values()), "byteSetExact": actual_bytes == expected_bytes,
               "missingGoldenRecords": len(expected_bytes-actual_bytes), "additionalCRecords": len(actual_bytes-expected_bytes),
               "fieldsExactRecords": exact_fields, "originalCRCSuffixExactRecords": exact_crc,
               "scopeAndOriginalCRCExact": scope_exact, "retryCallsExact": retry_calls_exact,
               "independentGateExact": gate_exact, "M3DeepCalls": summary["M3DeepCalls"],
               "M4DeepCalls": summary["M4DeepCalls"], "protocolCalls": summary["ProtocolCalls"],
               "detectedCPPeaks": summary["cpPeaks"], "syncHypotheses": summary["syncSuccess"],
               "CFOHypotheses": summary["cfoSuccess"], "symbolHypotheses": summary["symbolSuccess"],
               "frameCandidates": summary["frameCandidates"], "CRCChecks": summary["CRCChecked"],
               "remoteFourMessages": int(types >= {0,1,4,5}), "firstFailureStage": summary.get("InitialFailureStage", ""),
               "finalFailureStage": summary["failureStage"], "stopReason": summary["StopReason"],
               "sourceFingerprint": summary["SourceFingerprint"], "passed": passed}
        parity_rows.append(row)
        by_protocol[case["Protocol"]].append(row)
    for protocol, rows in by_protocol.items():
        counts = Counter(row["finalFailureStage"] for row in rows if not int(row["M5Flow"]))
        protocol_rows.append({"protocol": protocol, "candidates": len(rows),
                              **{column: sum(int(row[column]) for row in rows) for column in
                                 ("generatedFrames", "framesInsideCrop", "recoveredPhysical", "CRCRecords", "fields", "M4Fast", "M4Final", "M5Flow", "strict")},
                              "failureStages": json.dumps(counts, sort_keys=True)})
    baseline_path = ROOT.parent / "results/00_SUMMARY/optimization_baseline_cases.csv"
    old_success = {row["Signal"] for row in read(baseline_path) if row["M5Status"] == "SUCCESS"} if baseline_path.exists() else {cid for cid, row in cases.items() if row["M4Status"] == "COMPLETE"}
    fingerprints = {row["sourceFingerprint"] for row in parity_rows}
    version_header = ROOT / "results_c_validation/final/build/generated/wrj_port_version.h"
    build_fingerprint = re.search(r'#define SOURCE_FINGERPRINT "([a-f0-9]+)"', version_header.read_text())[1]
    report = {"cases": len(parity_rows), "passed": sum(row["passed"] for row in parity_rows),
              "oneSourceFingerprint": len(fingerprints) == 1, "sourceFingerprints": sorted(fingerprints),
              "sourceFingerprintMatchesBuild": fingerprints == {build_fingerprint},
              **{column: sum(int(row[column]) for row in parity_rows) for column in
                 ("M4Fast", "deepAttempted", "deepRecovered", "M4Final", "M5Flow", "strict", "CRCRecords", "fields", "recoveredPhysical", "framesInsideCrop", "remoteFourMessages")},
              "MOVED_RECOVERY": sum(int(row["deepRecovered"]) for row in parity_rows if row["candidate"] in old_success),
              "FASTEvidenceParityCases": sum(row["FASTBytesCRCFieldsExact"] for row in parity_rows),
              "FASTCRCRecords": sum(row["FASTCRCRecords"] for row in parity_rows),
              "NEW_RECOVERY": sum(int(row["M5Flow"]) for row in parity_rows if row["candidate"] not in old_success),
              "semanticCompleteCandidates": sum(int(row["M5Flow"]) for row in parity_rows if row["independentGateExact"]),
              "remoteCore": sum(int(row["M5Flow"]) for row in parity_rows if row["protocol"] == "RemoteID"),
              "droneIDComplete": sum(int(row["M5Flow"]) for row in parity_rows if row["protocol"] == "DJI DroneID"),
              "M1M2Runs": 0, "ARMExecuted": False,
              "strictDefinition": "C independent M5 flow AND frozen M2 link/protocol evaluation, comparator only",
              "physicalDefinition": "complete truth frame inside crop AND exact received Golden bytes/original CRC; truth stays offline"}
    report["originalSuccessSetPreserved"] = old_success.issubset({row["candidate"] for row in parity_rows if int(row["M5Flow"])})
    # Equality remains strict for every received byte/CRC/field/status/route.
    # Aggregate denominators follow the freshly measured MATLAB Golden.
    report["accepted"] = (report["cases"] == 53 and report["passed"] == 53 and report["oneSourceFingerprint"] and
                          report["sourceFingerprintMatchesBuild"] and report["originalSuccessSetPreserved"] and
                          report["FASTEvidenceParityCases"] == 53 and report["semanticCompleteCandidates"] == report["M5Flow"])
    binary = ROOT / "results_c_validation/final/build/wrj_c99_host.exe"
    if binary.exists():
        report["hostExecutableSHA256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    write(base / (prefix + "host_parity.csv"), parity_rows)
    write(base / (prefix + "protocol_summary.csv"), protocol_rows)
    write(base / (prefix + "physical_recovery.csv"), frame_rows)
    (base / (prefix + "acceptance.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    if not report["accepted"] and not args.allow_progress:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
