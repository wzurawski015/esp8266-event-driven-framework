#!/usr/bin/env python3
"""Parse ATNEL I2C HIL serial logs with strict sda-stuck-low evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "docs" / "release" / "hil_atnel_i2c_report.md"
DEFAULT_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "i2c" / "current"
GLOBAL_PASS = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0")
CASE_PASS = re.compile(r"EV_HIL_I2C_CASE_RESULT\s+name=sda-stuck-low-containment\s+status=PASS")
CASE_FAIL = re.compile(r"EV_HIL_I2C_CASE_RESULT\s+name=sda-stuck-low-containment\s+status=FAIL(?:\s+reason=([^\s]+))?")
COUPLED = re.compile(r"EV_HIL_I2C_SDA_FORCE_LOW\s+requested=1\s+observed=1")
UNCoupled = re.compile(r"EV_HIL_I2C_SDA_FORCE_LOW\s+requested=1\s+observed=0|FIXTURE_NOT_COUPLED")
RECOVERY_PASS = re.compile(r"EV_HIL_I2C_RECOVERY_RESULT\s+status=OK")
SECRET_RE = re.compile(r"(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*")


def redact(text: str) -> str:
    return SECRET_RE.sub(lambda m: m.group(1) + "=<REDACTED>", text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_text(text: str) -> dict[str, object]:
    redacted = redact(text)
    fixture_not_coupled = bool(UNCoupled.search(redacted))
    has_global_pass = bool(GLOBAL_PASS.search(redacted))
    has_case_pass = bool(CASE_PASS.search(redacted))
    has_coupled = bool(COUPLED.search(redacted))
    has_recovery_pass = bool(RECOVERY_PASS.search(redacted))
    fail_reason = None
    m = CASE_FAIL.search(redacted)
    if m:
        fail_reason = m.group(1) or "CASE_FAIL"
    failures = []
    if fixture_not_coupled:
        failures.append("FIXTURE_NOT_COUPLED")
    if not has_global_pass:
        failures.append("missing EV_HIL_RESULT PASS failures=0 skipped=0")
    if not has_case_pass:
        failures.append("missing sda-stuck-low-containment PASS marker")
    if not has_coupled:
        failures.append("missing observed SDA-low coupling marker")
    if not has_recovery_pass:
        failures.append("missing recovery PASS/OK marker")
    if fail_reason:
        failures.append(f"case failure marker: {fail_reason}")
    return {
        "status": "PASS" if not failures else "FAIL",
        "case": "sda-stuck-low-containment",
        "global_pass": has_global_pass,
        "case_pass": has_case_pass,
        "fixture_coupled": has_coupled and not fixture_not_coupled,
        "recovery_pass": has_recovery_pass,
        "failures": failures,
    }


def write_report(parsed: dict[str, object], evidence_dir: Path, serial_log: Path | None = None) -> None:
    status = str(parsed.get("status", "FAIL"))
    reason = "all required serial markers observed" if status == "PASS" else "; ".join(parsed.get("failures", []))
    log_rel = serial_log.relative_to(ROOT).as_posix() if serial_log and serial_log.is_file() else ""
    parsed_rel = (evidence_dir / "parsed.json").relative_to(ROOT).as_posix()
    REPORT.write_text(
        "# ATNEL I2C HIL report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_rel}` |\n"
        f"| Parsed evidence | `{parsed_rel}` |\n\n"
        "PASS requires `EV_HIL_RESULT PASS failures=0 skipped=0`, "
        "`EV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS`, "
        "an observed SDA-low coupling marker, and recovery OK.\n",
        encoding="utf-8",
    )


def write_evidence(text: str, evidence_dir: Path, source: Path | None = None) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    serial = evidence_dir / "serial.log"
    serial.write_text(redact(text), encoding="utf-8")
    parsed = parse_text(text)
    parsed.update({"serial_log": serial.relative_to(ROOT).as_posix(), "serial_sha256": sha256(serial)})
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "excerpt.md").write_text("# ATNEL I2C HIL excerpt\n\n```text\n" + serial.read_text()[-4000:] + "\n```\n", encoding="utf-8")
    (evidence_dir / "sha256sums.txt").write_text(
        f"{sha256(serial)}  serial.log\n{sha256(evidence_dir / 'parsed.json')}  parsed.json\n",
        encoding="utf-8",
    )
    write_report(parsed, evidence_dir, serial)
    return 0 if parsed["status"] == "PASS" else 1


def environment_blocked(reason: str) -> int:
    DEFAULT_EVIDENCE.mkdir(parents=True, exist_ok=True)
    parsed = {"status": "ENVIRONMENT_BLOCKED", "case": "sda-stuck-low-containment", "reason": reason, "failures": [reason]}
    (DEFAULT_EVIDENCE / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    REPORT.write_text(
        "# ATNEL I2C HIL report\n\n"
        "| Field | Value |\n|---|---|\n"
        "| Status | ENVIRONMENT_BLOCKED |\n"
        f"| Reason | {reason} |\n"
        f"| Parsed evidence | `{(DEFAULT_EVIDENCE / 'parsed.json').relative_to(ROOT).as_posix()}` |\n\n"
        "No PASS is declared without real serial evidence.\n",
        encoding="utf-8",
    )
    print(f"EV_HIL_ATNEL_I2C ENVIRONMENT_BLOCKED reason={reason}")
    return 77


def self_test() -> int:
    valid = """
EV_HIL_I2C_CASE_BEGIN name=sda-stuck-low-containment
EV_HIL_I2C_SDA_FORCE_LOW requested=1 observed=1
EV_HIL_I2C_RECOVERY_RESULT status=OK
EV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS
EV_HIL_RESULT PASS failures=0 skipped=0
"""
    assert parse_text(valid)["status"] == "PASS"
    assert parse_text(valid.replace("EV_HIL_RESULT PASS failures=0 skipped=0", ""))["status"] == "FAIL"
    assert parse_text(valid.replace("observed=1", "observed=0"))["status"] == "FAIL"
    assert "<REDACTED>" in redact("WIFI_PASSWORD=secret")
    print("ATNEL_I2C_HIL_LOG_PARSER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", type=Path)
    ap.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE)
    ap.add_argument("--environment-blocked", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.environment_blocked or args.log is None:
        return environment_blocked("ATNEL I2C hardware fixture or serial log is not available")
    return write_evidence(args.log.read_text(encoding="utf-8", errors="ignore"), args.evidence_dir, args.log)

if __name__ == "__main__":
    raise SystemExit(main())
