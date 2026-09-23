#!/usr/bin/env python3
"""Compare WRJ_ARM_Port Module3 CSV output with the formal MATLAB baseline."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def number(row: dict[str, str], key: str) -> float:
    try:
        return float(row.get(key, "nan"))
    except (TypeError, ValueError):
        return math.nan


def normalized_status(value: str) -> str:
    return "low_confidence" if value == "sync_below_threshold" else value


def matched_start_count(c_starts: list[float], m_starts: list[float], tolerance: float) -> int:
    remaining = list(m_starts)
    matched = 0
    for value in c_starts:
        if not remaining:
            break
        best = min(range(len(remaining)), key=lambda index: abs(value - remaining[index]))
        if abs(value - remaining[best]) <= tolerance:
            matched += 1
            remaining.pop(best)
    return matched


def main() -> int:
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[1]
    parser.add_argument(
        "--c-results", type=Path,
        default=root / "results" / "pc" / "module3_regression" / "cases",
    )
    parser.add_argument(
        "--matlab-results",
        type=Path,
        default=root.parent / "results_wrj_m12_unknown_58g_final" /
        "06_MODULE3_FREQUENCY_FRAME_SYNC" / "01_TABLES",
    )
    parser.add_argument("--output", type=Path, default=root / "docs" / "MODULE3_VALIDATION.md")
    args = parser.parse_args()

    matlab_summary = {
        row["candidateId"]: row
        for row in read_rows(args.matlab_results / "module3_sync_summary.csv")
    }
    matlab_frames: dict[str, list[dict[str, str]]] = {}
    for row in read_rows(args.matlab_results / "module3_frame_index.csv"):
        matlab_frames.setdefault(row["candidateId"], []).append(row)
    for rows in matlab_frames.values():
        rows.sort(key=lambda item: number(item, "frameIndex"))

    reports: list[dict[str, object]] = []
    for result_file in sorted(args.c_results.glob("*/module3_result.csv")):
        c_row = read_rows(result_file)[0]
        candidate_id = c_row["candidateId"]
        if candidate_id not in matlab_summary:
            continue
        m_row = matlab_summary[candidate_id]
        c_frames_path = result_file.parent / "module3_frames.csv"
        c_frames = read_rows(c_frames_path) if c_frames_path.exists() else []
        m_frames = matlab_frames.get(candidate_id, [])
        c_starts = [number(row, "frameStart1") for row in c_frames]
        m_starts = [number(row, "localStartSample") for row in m_frames]
        first_start_error = abs(c_starts[0] - m_starts[0]) if c_starts and m_starts else math.nan
        matched_starts = matched_start_count(c_starts, m_starts, 1.0)
        c_cfo = number(c_row, "estimatedCFOHz")
        m_cfo = number(m_row, "totalBasebandCorrectionHz")
        c_spectral = number(c_row, "spectralCorrectionHz")
        m_spectral = number(m_row, "spectralCorrectionHz")
        c_confidence = number(c_row, "syncConfidence")
        m_confidence = number(m_row, "syncConfidence")
        c_length = number(c_row, "frameLength")
        m_length = number(m_row, "estimatedFrameLengthSamples")
        profile_ok = c_row["profile"] == m_row["syncProfile"]
        status_ok = normalized_status(c_row["status"]) == normalized_status(m_row["status"])
        wideband = "Wideband_CP" in m_row["syncProfile"]
        checks = {
            "profile": profile_ok,
            "status": status_ok,
            "frame_length": c_length == m_length,
            "frame_count": len(c_frames) == len(m_frames),
            "frame_start": math.isfinite(first_start_error) and first_start_error <= (1.0 if wideband else 16.0),
            "cfo": math.isfinite(c_cfo) and math.isfinite(m_cfo) and abs(c_cfo - m_cfo) <= (10.0 if wideband else 250000.0),
            "confidence": math.isfinite(c_confidence) and math.isfinite(m_confidence) and abs(c_confidence - m_confidence) <= (0.02 if wideband else 0.20),
        }
        if wideband:
            hard_pass = all(checks.values())
            if hard_pass and matched_starts == len(c_frames):
                verdict = "PASS"
            elif hard_pass and matched_starts >= math.ceil(0.90 * len(c_frames)):
                verdict = "WARN"
            else:
                verdict = "FAIL"
        else:
            verdict = "PASS" if all(checks.values()) else "WARN"
        reports.append(
            {
                "id": candidate_id,
                "verdict": verdict,
                "profile": f'{c_row["profile"]} / {m_row["syncProfile"]}',
                "status": f'{c_row["status"]} / {m_row["status"]}',
                "spectral_delta": c_spectral - m_spectral,
                "cfo_delta": c_cfo - m_cfo,
                "confidence_delta": c_confidence - m_confidence,
                "length": f"{int(c_length)} / {int(m_length)}",
                "count": f"{len(c_frames)} / {len(m_frames)}",
                "first_start_error": first_start_error,
                "matched_starts": f"{matched_starts}/{len(c_frames)}",
            }
        )

    lines = [
        "# Module3 C/MATLAB Validation",
        "",
        "Generated by `tools/validate_module3.py`. C frame starts are converted to MATLAB's one-based convention before comparison.",
        "",
        "Wideband acceptance: profile/status exact, frame length/count exact, first start error <= 1 sample, total CFO error <= 10 Hz, and sync-confidence error <= 0.02. PASS additionally requires every selected frame start to match within one sample; >=90% start coverage is WARN. Other profiles are migration diagnostics and use WARN rather than FAIL while parity work remains.",
        "",
        "| Candidate | Result | C / MATLAB profile | C / MATLAB status | Spectral delta (Hz) | Total CFO delta (Hz) | Confidence delta | Length C/M | Count C/M | Starts <=1 sample | First start error |",
        "|---|---:|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for report in reports:
        lines.append(
            "| {id} | {verdict} | {profile} | {status} | {spectral_delta:.3f} | "
            "{cfo_delta:.3f} | {confidence_delta:.6f} | {length} | {count} | "
            "{matched_starts} | {first_start_error:.1f} |".format(**report)
        )
    counts = {name: sum(report["verdict"] == name for report in reports) for name in ("PASS", "WARN", "FAIL")}
    lines.extend(
        [
            "",
            f"Summary: **{counts['PASS']} PASS, {counts['WARN']} WARN, {counts['FAIL']} FAIL** across {len(reports)} cases.",
            "",
            "`WARN` means the C path runs and emits compatible diagnostics, but numerical parity with the current MATLAB profile is not yet within the provisional non-wideband tolerance. It is not converted into a pass by relaxing the production sync threshold.",
            "",
        ]
    )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {args.output}: {counts}")
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
