"""Verify the recorded frozen sources and old reference; never modify them."""
import csv
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAT = ROOT.parent
OLD = MAT.parent / "WRJ_Module1234_ProjectDemo_CN_v8_3_Standalone"
DOC = MAT / "docs/cleanup"
AUTHORIZED_C_CHANGES={'WRJ_ARM_Port/CMakeLists.txt','WRJ_ARM_Port/include/wrj_phy.h',
    'WRJ_ARM_Port/include/wrj_receiver.h','WRJ_ARM_Port/include/wrj_recovery_machine.h',
    'WRJ_ARM_Port/src/module4/e34_ofdm.c','WRJ_ARM_Port/src/module4/e34_phy.c',
    'WRJ_ARM_Port/src/module4/e34_protocol.c','WRJ_ARM_Port/src/module5/e34_recovery_machine.c'}


def read(path):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def verify(root, manifest):
    rows = []
    for reference in read(manifest):
        path = root / reference["path"]
        sha = hashlib.sha256(path.read_bytes()).hexdigest().upper() if path.is_file() else "MISSING"
        rows.append({"path": reference["path"], "passed": sha == reference["sha256"].upper(), "sha256": sha})
    return rows


def sha(path):
    if not path.is_file(): return "MISSING"
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""): h.update(block)
    return h.hexdigest().upper()

sources = read(MAT / "results/source_sha256.csv")
expected = {"Module1.m", "Module2.m", "Module3.m", "Module4.m", "Module5.m",
            "Common_Functions.m", "WRJ_Config.m", "WRJ_GUI.m", "WRJ_Main.m"}
assert len(sources) == 9 and {r["File"] for r in sources} == expected
matlab = [{"path": r["File"], "passed": sha(MAT/r["File"]) == r["SHA256"].upper()} for r in sources]
protected = [{"path": r["Path"], "passed": sha(MAT/r["Path"]) == r["SHA256"].upper()}
             for r in read(DOC/"protected_before.csv") if r["Path"] != "Common_Functions.m" and r['Path'] not in AUTHORIZED_C_CHANGES]
frozen = [{"path": r['Path'], "passed":sha(MAT/r['Path'])==r['SHA256'].upper()}
          for r in read(DOC/'production_during_fresh.csv') if r['Path'] not in {'Module3.m','Module4.m','Module5.m'}]
iq = [{"path": r["Path"], "passed": sha(MAT/r["Path"]) == r["SHA256"].upper()} for r in read(DOC/"iq_before.csv")]
old = [{"path": r["Path"], "passed": sha(OLD/r["Path"]) == r["SHA256"].upper()} for r in read(DOC/"old_reference_before.csv")]
old_set_exact = {p.relative_to(OLD).as_posix() for p in OLD.rglob("*") if p.is_file()} == {r["path"] for r in old}
old_c = [r for r in old if r["path"].startswith("WRJ_ARM_Port/")]
git_changed = [r["path"] for r in protected if r["path"].startswith("WRJ_ARM_Port/.git/") and not r["passed"]]
report = {"MATLABFiles": len(matlab), "MATLABUnchanged": sum(row["passed"] for row in matlab),
          "OldCFiles": len(old_c), "OldCUnchanged": sum(row["passed"] for row in old_c),
          "OldStandaloneFiles": len(old), "OldStandaloneUnchanged": sum(r["passed"] for r in old),
          "OldStandaloneFileSetExact": old_set_exact,
          "IQFiles": len(iq), "IQUnchanged": sum(r["passed"] for r in iq),
          "ProtectedUnchanged": all(r["passed"] for r in protected+frozen),
          "AuthorizedCChanges": sorted(AUTHORIZED_C_CHANGES),
          "M1M2Runs": 0, "GitCommits": 0, "ARMExecutions": 0,
          "CopiedGitMetadataChanged": git_changed, "matlab": matlab, "oldC": old_c}
report["accepted"] = all(r["passed"] for r in matlab+protected+frozen+iq+old) and old_set_exact
(ROOT/"results_c_validation/final/integrity.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
print(json.dumps({key: value for key, value in report.items() if key not in ("matlab", "oldC")}, indent=2))
if not report["accepted"]:
    raise SystemExit(1)
