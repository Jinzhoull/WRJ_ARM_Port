"""Summarize C Module4 real-IQ results; compare only independent MATLAB IQ decodes."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def first_row(path: Path) -> dict[str, str]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return next(csv.DictReader(handle))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--matlab-remoteid", type=Path)
    args = parser.parse_args()
    matlab: dict[str, dict[str, str]] = {}
    if args.matlab_remoteid:
        with args.matlab_remoteid.open("r", encoding="utf-8-sig", newline="") as handle:
            matlab = {row["candidateId"]: row for row in csv.DictReader(handle)}

    print("candidate,profile,byteSource,byteRecoveryStatus,numRecoveredBytes,"
          "byteConfidence,crcChecked,crcPassed,packetCount,fieldCount,"
          "parseStatus,parseComplete,matlabIqAgreement")
    seen: set[str] = set()
    for result_path in sorted(args.results.glob("*/module4_result.csv")):
        row = first_row(result_path)
        candidate = row["candidateId"]
        if candidate in seen:
            continue
        seen.add(candidate)
        comparison = "not_available"
        reference = matlab.get(candidate)
        if reference and reference.get("candidateIqDecoded") == "1" and \
                reference.get("decodeSource") == "candidate_iq":
            match = (row.get("uasId") == reference.get("decodedUasId") and
                     abs(float(row.get("latitudeDeg", "nan")) -
                         float(reference.get("decodedLatitudeDeg", "nan"))) < 1e-5 and
                     abs(float(row.get("longitudeDeg", "nan")) -
                         float(reference.get("decodedLongitudeDeg", "nan"))) < 1e-5 and
                     abs(float(row.get("altitudeM", "nan")) -
                         float(reference.get("decodedAltitudeM", "nan"))) < 0.1 and
                     abs(float(row.get("speedMps", "nan")) -
                         float(reference.get("decodedSpeedMps", "nan"))) < 0.1 and
                     abs(float(row.get("headingDeg", "nan")) -
                         float(reference.get("decodedHeadingDeg", "nan"))) < 0.1)
            comparison = "match" if match else "different_or_incomplete"
        values = [candidate, row["protocolType"], row["byteSource"],
                  row["byteRecoveryStatus"], row["byteCount"],
                  row["byteRecoveryConfidence"], row["crcChecked"],
                  row["crcPassed"], row["packetCount"], row["fieldCount"],
                  row["parseStatus"], row["parseComplete"], comparison]
        print(",".join(values))


if __name__ == "__main__":
    main()
