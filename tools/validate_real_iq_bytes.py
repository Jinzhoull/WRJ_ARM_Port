"""Compare C Module4 and MATLAB iqRecoveredBytes on the same real aligned IQ.

Only ``records.iqRecoveredBytes`` is used. ``records.bytes`` is deliberately
ignored because it may contain Synthetic Protocol Harness records.
"""

import argparse
import csv
import json
import subprocess
from pathlib import Path

import h5py
import numpy as np


def matlab_text(mat, reference):
    return "".join(chr(int(x)) for x in np.asarray(mat[reference]).reshape(-1))


def to_bits(data):
    return np.unpackbits(np.frombuffer(data, dtype=np.uint8), bitorder="big")


def compare(reference, actual):
    best = None
    for offset in range(-16, 17):
        left = max(0, -offset)
        right = max(0, offset)
        size = min(len(reference) - left, len(actual) - right)
        if size < 16:
            continue
        count = sum(reference[left + i] == actual[right + i] for i in range(size))
        score = count / size
        if best is None or score > best[0]:
            best = (score, offset, left, right, size)
    if best is None:
        return dict(byteAgreement=0.0, bestByteOffset=0, bitAgreement=0.0,
                    bestBitOffset=0, polarityInvariantByteAgreement=0.0,
                    longestMatchingByteRun=0, mismatchPositions="")
    score, offset, left, right, size = best
    matches = [reference[left + i] == actual[right + i] for i in range(size)]
    longest = run = 0
    for match in matches:
        run = run + 1 if match else 0
        longest = max(longest, run)
    a, b = to_bits(reference), to_bits(actual)
    bit_best = (0.0, 0)
    for shift in range(-32, 33):
        ref_start = max(0, -shift)
        actual_start = max(0, shift)
        n = min(len(a) - ref_start, len(b) - actual_start)
        if n < 128:
            continue
        agreement = float(np.mean(a[ref_start:ref_start + n] ==
                                  b[actual_start:actual_start + n]))
        if agreement > bit_best[0]:
            bit_best = (agreement, shift)
    polarity_score = 0.0
    for trial in (actual, bytes(value ^ 0xFF for value in actual)):
        for trial_offset in range(-16, 17):
            ref_start = max(0, -trial_offset)
            actual_start = max(0, trial_offset)
            n = min(len(reference) - ref_start, len(trial) - actual_start)
            if n >= 16:
                polarity_score = max(polarity_score, sum(
                    reference[ref_start + i] == trial[actual_start + i]
                    for i in range(n)) / n)
    return dict(byteAgreement=round(score, 6), bestByteOffset=offset,
                bitAgreement=round(bit_best[0], 6), bestBitOffset=bit_best[1],
                polarityInvariantByteAgreement=round(polarity_score, 6),
                longestMatchingByteRun=longest,
                mismatchPositions="|".join(str(i) for i, value in enumerate(matches) if not value))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--observations", type=Path, required=True)
    parser.add_argument("--aligned-dir", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--candidate", action="append", required=True)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    results = []
    with h5py.File(str(args.observations), "r") as observations:
        records = observations["records"]
        for candidate in args.candidate:
            aligned_files = list(args.aligned_dir.glob("*__" + candidate + ".mat"))
            if len(aligned_files) != 1:
                raise RuntimeError("aligned MAT missing or ambiguous: " + candidate)
            profile = "droneid" if "droneid" in candidate else "wideband"
            with h5py.File(str(aligned_files[0]), "r") as aligned:
                frame_bank = aligned["alignedFrames"]
                seen = set()
                for index in range(records["candidateId"].shape[0]):
                    if matlab_text(observations, records["candidateId"][index, 0]) != candidate:
                        continue
                    frame_index = int(np.asarray(observations[
                        records["physicalFrameIndex"][index, 0]]).reshape(-1)[0])
                    if frame_index in seen or frame_index < 1 or frame_index > frame_bank.shape[1]:
                        continue
                    seen.add(frame_index)
                    reference = np.asarray(observations[
                        records["iqRecoveredBytes"][index, 0]]).reshape(-1).astype("u1").tobytes()
                    frame = np.asarray(aligned[frame_bank[0, frame_index - 1]]).reshape(-1)
                    iq = np.empty((len(frame), 2), dtype="<f4")
                    iq[:, 0] = frame["real"]
                    iq[:, 1] = frame["imag"]
                    command = [str(args.probe), profile, str(len(frame))]
                    output = subprocess.run(command, input=iq.tobytes(),
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                            check=True).stdout.decode("ascii").strip()
                    parts = dict(part.split("=", 1) for part in output.split(" "))
                    actual = bytes.fromhex(parts["hex"])
                    row = dict(candidate=candidate, frameIndex=frame_index,
                               matlabByteCount=len(reference), cByteCount=len(actual),
                               matlabMethod=matlab_text(observations,
                                   records["decoderMethod"][index, 0]),
                               cMethod=parts["method"],
                               cDetails=parts.get("sps", ""),
                               matlabPhase=float(np.asarray(observations[
                                   records["phaseCorrectionRad"][index, 0]]).reshape(-1)[0]),
                               matlabTiming=float(np.asarray(observations[
                                   records["symbolTimingConsistency"][index, 0]]).reshape(-1)[0]),
                               matlabFirst64=reference[:64].hex().upper(),
                               cFirst64=actual[:64].hex().upper())
                    row.update(compare(reference, actual))
                    results.append(row)
    with args.out.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(results[0].keys()))
        writer.writeheader()
        writer.writerows(results)
    for candidate in args.candidate:
        rows = [row for row in results if row["candidate"] == candidate]
        print(json.dumps(dict(candidate=candidate, frames=len(rows),
                              meanByteAgreement=round(float(np.mean(
                                  [r["byteAgreement"] for r in rows])), 5),
                              meanBitAgreement=round(float(np.mean(
                                  [r["bitAgreement"] for r in rows])), 5),
                              polarityInvariantByteAgreement=round(float(np.mean(
                                  [r["polarityInvariantByteAgreement"] for r in rows])), 5),
                              methods=sorted(set(r["matlabMethod"] for r in rows))),
                         ensure_ascii=False))


if __name__ == "__main__":
    main()
