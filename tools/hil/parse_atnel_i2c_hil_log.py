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
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_redaction import redact_text
REPORT = ROOT / "docs" / "release" / "hil_atnel_i2c_report.md"
DEFAULT_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "i2c" / "current"
GLOBAL_PASS = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0")
CASE_BEGIN = re.compile(r"EV_HIL_I2C_CASE_BEGIN\s+name=sda-stuck-low-containment")
CASE_PASS = re.compile(r"EV_HIL_I2C_CASE_RESULT\s+name=sda-stuck-low-containment\s+status=PASS")
CASE_FAIL = re.compile(r"EV_HIL_I2C_CASE_RESULT\s+name=sda-stuck-low-containment\s+status=FAIL(?:\s+reason=([^\s]+))?")
COUPLED = re.compile(r"EV_HIL_I2C_SDA_FORCE_LOW\s+requested=1\s+observed=1")
UNCOUPLED = re.compile(r"EV_HIL_I2C_SDA_FORCE_LOW\s+requested=1\s+observed=0|FIXTURE_NOT_COUPLED")
BUS_STATE = re.compile(r"EV_HIL_I2C_BUS_STATE")
RECOVERY_BEGIN = re.compile(r"EV_HIL_I2C_RECOVERY_BEGIN")
RECOVERY_PASS = re.compile(r"EV_HIL_I2C_RECOVERY_RESULT\s+status=(?:PASS|OK)")
ACK_EVIDENCE = re.compile(r"EV_HIL_I2C_ACK_EVIDENCE\s+name=read-stream-completion")
WRITE_NACK_EVIDENCE = re.compile(r"EV_HIL_I2C_NACK_EVIDENCE\s+name=missing-device-write-nack-stop-release")
READ_NACK_EVIDENCE = re.compile(r"EV_HIL_I2C_NACK_EVIDENCE\s+name=missing-device-read-nack-stop-release")
FINAL_NACK_SENT = re.compile(r"EV_HIL_I2C_FINAL_NACK_SENT\s+name=read-stream-completion")
STOP_RELEASE_OK = re.compile(r"EV_HIL_I2C_STOP_RELEASE\s+name=[^\s]+.*?ok=([1-9][0-9]*).*?fail=0.*?sda=1\s+scl=1")
READ_STREAM_STOP_RELEASE_OK = re.compile(r"EV_HIL_I2C_STOP_RELEASE\s+name=read-stream-completion.*?ok=([1-9][0-9]*).*?fail=0.*?sda=1\s+scl=1")
WRITE_NACK_STOP_RELEASE_OK = re.compile(r"EV_HIL_I2C_STOP_RELEASE\s+name=missing-device-write-nack-stop-release.*?ok=([1-9][0-9]*).*?fail=0.*?sda=1\s+scl=1")
READ_NACK_STOP_RELEASE_OK = re.compile(r"EV_HIL_I2C_STOP_RELEASE\s+name=missing-device-read-nack-stop-release.*?ok=([1-9][0-9]*).*?fail=0.*?sda=1\s+scl=1")
BOARD_PIN_MAP = re.compile(r"EV_HIL_BOARD_PIN_MAP\s+board=\S+\s+i2c_port=0\s+scl_gpio=\d+\s+sda_gpio=\d+\s+onewire_gpio=\d+\s+source=bsp/atnel_air_esp_motherboard/pins.def")
SPEED_POLICY = re.compile(r"EV_HIL_I2C_SPEED_POLICY\s+.*safe_hz=100000.*fast_hz=400000.*fast_requires_logic_analyzer=1")
RECOVERY_EVIDENCE = re.compile(r"EV_HIL_I2C_RECOVERY_EVIDENCE\s+case=sda-stuck-low-containment.*?pulses=\d+.*?sda=1\s+scl=1")
CLOCK_STRETCH_EVIDENCE = re.compile(r"EV_HIL_I2C_CLOCK_STRETCH_EVIDENCE\s+case=scl-held-low-timeout.*?scl_held_low_case=PASS")
MUTEX_EVIDENCE = re.compile(r"EV_HIL_I2C_MUTEX_EVIDENCE\s+name=[^\s]+.*?unbalanced=0")
SCAN_NACK_POLICY = re.compile(r"EV_HIL_I2C_SCAN_NACK_POLICY\s+addr7=0x[0-7][0-9A-Fa-f]\s+status=NACK\s+recovery_delta=0\s+stop_release_ok=1")
SECRET_RE = re.compile(r"(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*")
PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)


def redact(text: str) -> str:
    return redact_text(text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def is_placeholder_path(path: Path | str) -> bool:
    return bool(PLACEHOLDER_RE.search(str(path)))


def safe_read_log(path: Path) -> tuple[str, str, str]:
    if is_placeholder_path(path):
        return "ENVIRONMENT_BLOCKED", f"placeholder path was supplied: {path}", ""
    try:
        if not path.exists():
            return "ENVIRONMENT_BLOCKED", f"log file not found: {path}", ""
        if path.is_dir():
            return "FAIL", f"input path is directory, expected serial log file: {path}", ""
        return "PASS", "", path.read_text(encoding="utf-8", errors="ignore")
    except PermissionError as exc:
        return "FAIL", f"unable to read serial log: {path}: {exc}", ""
    except OSError as exc:
        return "FAIL", f"unable to read serial log: {path}: {exc}", ""


def parse_text(text: str) -> dict[str, object]:
    redacted = redact(text)
    fixture_not_coupled = bool(UNCOUPLED.search(redacted))
    has_global_pass = bool(GLOBAL_PASS.search(redacted))
    has_case_begin = bool(CASE_BEGIN.search(redacted))
    has_case_pass = bool(CASE_PASS.search(redacted))
    has_coupled = bool(COUPLED.search(redacted))
    has_bus_state = bool(BUS_STATE.search(redacted))
    has_recovery_begin = bool(RECOVERY_BEGIN.search(redacted))
    has_recovery_pass = bool(RECOVERY_PASS.search(redacted))
    has_ack_evidence = bool(ACK_EVIDENCE.search(redacted))
    has_write_nack_evidence = bool(WRITE_NACK_EVIDENCE.search(redacted))
    has_read_nack_evidence = bool(READ_NACK_EVIDENCE.search(redacted))
    has_final_nack_sent = bool(FINAL_NACK_SENT.search(redacted))
    has_stop_release_ok = bool(STOP_RELEASE_OK.search(redacted))
    has_read_stream_stop_release_ok = bool(READ_STREAM_STOP_RELEASE_OK.search(redacted))
    has_write_nack_stop_release_ok = bool(WRITE_NACK_STOP_RELEASE_OK.search(redacted))
    has_read_nack_stop_release_ok = bool(READ_NACK_STOP_RELEASE_OK.search(redacted))
    has_board_pin_map = bool(BOARD_PIN_MAP.search(redacted))
    has_speed_policy = bool(SPEED_POLICY.search(redacted))
    has_recovery_evidence = bool(RECOVERY_EVIDENCE.search(redacted))
    has_clock_stretch_evidence = bool(CLOCK_STRETCH_EVIDENCE.search(redacted))
    has_mutex_evidence = bool(MUTEX_EVIDENCE.search(redacted))
    has_scan_nack_policy = bool(SCAN_NACK_POLICY.search(redacted))
    fail_reason = None
    m = CASE_FAIL.search(redacted)
    if m:
        fail_reason = m.group(1) or "CASE_FAIL"
    failures = []
    if fixture_not_coupled:
        failures.append("FIXTURE_NOT_COUPLED")
    if not has_global_pass:
        failures.append("missing EV_HIL_RESULT PASS failures=0 skipped=0")
    if not has_board_pin_map:
        failures.append("missing EV_HIL_BOARD_PIN_MAP marker")
    if not has_speed_policy:
        failures.append("missing EV_HIL_I2C_SPEED_POLICY marker")
    if not has_case_begin:
        failures.append("missing EV_HIL_I2C_CASE_BEGIN marker")
    if not has_case_pass:
        failures.append("missing sda-stuck-low-containment PASS marker")
    if not has_coupled:
        failures.append("missing observed SDA-low coupling marker")
    if not has_bus_state:
        failures.append("missing EV_HIL_I2C_BUS_STATE marker")
    if not has_recovery_begin:
        failures.append("missing EV_HIL_I2C_RECOVERY_BEGIN marker")
    if not has_recovery_pass:
        failures.append("missing recovery PASS/OK marker")
    if not has_recovery_evidence:
        failures.append("missing bounded recovery evidence marker")
    if not has_clock_stretch_evidence:
        failures.append("missing clock-stretch/held-low evidence marker")
    if not has_mutex_evidence:
        failures.append("missing whole-transaction mutex evidence marker")
    if not has_scan_nack_policy:
        failures.append("missing scan/missing-address NACK no-recovery policy marker")
    if not has_ack_evidence:
        failures.append("missing read_stream ACK evidence marker")
    if not has_write_nack_evidence:
        failures.append("missing write_stream NACK evidence marker")
    if not has_read_nack_evidence:
        failures.append("missing read_stream NACK evidence marker")
    if not has_final_nack_sent:
        failures.append("missing read_stream final-NACK evidence marker")
    if not has_stop_release_ok:
        failures.append("missing STOP-release idle evidence marker")
    if not has_read_stream_stop_release_ok:
        failures.append("missing read_stream STOP-release idle evidence marker")
    if not has_write_nack_stop_release_ok:
        failures.append("missing write_stream NACK STOP-release idle evidence marker")
    if not has_read_nack_stop_release_ok:
        failures.append("missing read_stream NACK STOP-release idle evidence marker")
    if fail_reason:
        failures.append(f"case failure marker: {fail_reason}")
    return {
        "status": "PASS" if not failures else "FAIL",
        "case": "sda-stuck-low-containment",
        "global_pass": has_global_pass,
        "case_begin": has_case_begin,
        "case_pass": has_case_pass,
        "board_pin_map": has_board_pin_map,
        "speed_policy": has_speed_policy,
        "fixture_coupled": has_coupled and not fixture_not_coupled,
        "bus_state": has_bus_state,
        "recovery_begin": has_recovery_begin,
        "recovery_pass": has_recovery_pass,
        "recovery_evidence": has_recovery_evidence,
        "clock_stretch_evidence": has_clock_stretch_evidence,
        "mutex_evidence": has_mutex_evidence,
        "scan_nack_policy": has_scan_nack_policy,
        "ack_evidence": has_ack_evidence,
        "write_nack_evidence": has_write_nack_evidence,
        "read_nack_evidence": has_read_nack_evidence,
        "nack_evidence": has_write_nack_evidence and has_read_nack_evidence,
        "final_nack_sent": has_final_nack_sent,
        "stop_release_ok": has_stop_release_ok,
        "read_stream_stop_release_ok": has_read_stream_stop_release_ok,
        "write_nack_stop_release_ok": has_write_nack_stop_release_ok,
        "read_nack_stop_release_ok": has_read_nack_stop_release_ok,
        "failures": failures,
    }


def write_report(parsed: dict[str, object], evidence_dir: Path, serial_log: Path | None = None) -> None:
    status = str(parsed.get("status", "FAIL"))
    reason = "all required serial markers observed" if status == "PASS" else "; ".join(parsed.get("failures", []))
    log_rel = serial_log.relative_to(ROOT).as_posix() if serial_log and serial_log.is_file() and serial_log.is_relative_to(ROOT) else ""
    parsed_rel = (evidence_dir / "parsed.json").relative_to(ROOT).as_posix() if (evidence_dir / "parsed.json").is_relative_to(ROOT) else str(evidence_dir / "parsed.json")
    REPORT.write_text(
        "# ATNEL I2C HIL report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_rel}` |\n"
        f"| Parsed evidence | `{parsed_rel}` |\n\n"
        "PASS requires `EV_HIL_RESULT PASS failures=0 skipped=0`, "
        "`EV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS`, "
        "observed SDA-low coupling, recovery OK, read_stream ACK/final-NACK markers, "
        "write_stream/read_stream NACK markers, and per-case STOP-release idle evidence. Placeholder or missing paths are reported as ENVIRONMENT_BLOCKED, not Python tracebacks.\n",
        encoding="utf-8",
    )


def write_evidence(text: str, evidence_dir: Path, source: Path | None = None) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    serial = evidence_dir / "serial.log"
    serial.write_text(redact(text), encoding="utf-8")
    parsed = parse_text(text)
    parsed.update({"serial_log": serial.relative_to(ROOT).as_posix() if serial.is_relative_to(ROOT) else str(serial), "serial_sha256": sha256(serial)})
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "excerpt.md").write_text("# ATNEL I2C HIL excerpt\n\n```text\n" + serial.read_text(encoding="utf-8")[-4000:] + "\n```\n", encoding="utf-8")
    (evidence_dir / "sha256sums.txt").write_text(
        f"{sha256(serial)}  serial.log\n{sha256(evidence_dir / 'parsed.json')}  parsed.json\n",
        encoding="utf-8",
    )
    if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
        write_report(parsed, evidence_dir, serial)
    return 0 if parsed["status"] == "PASS" else 1


def environment_blocked(reason: str, evidence_dir: Path = DEFAULT_EVIDENCE) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    parsed = {"status": "ENVIRONMENT_BLOCKED", "case": "sda-stuck-low-containment", "reason": reason, "failures": [reason]}
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
        REPORT.write_text(
            "# ATNEL I2C HIL report\n\n"
            "| Field | Value |\n|---|---|\n"
            "| Status | ENVIRONMENT_BLOCKED |\n"
            f"| Reason | {reason} |\n"
            f"| Parsed evidence | `{(evidence_dir / 'parsed.json').relative_to(ROOT).as_posix()}` |\n\n"
            "No PASS is declared without real serial evidence.\n",
            encoding="utf-8",
        )
    print(f"EV_HIL_ATNEL_I2C ENVIRONMENT_BLOCKED reason={reason}")
    return 77


def self_test() -> int:
    valid = """
EV_HIL_BOARD_PIN_MAP board=ev_atnel i2c_port=0 scl_gpio=4 sda_gpio=5 onewire_gpio=12 source=bsp/atnel_air_esp_motherboard/pins.def
EV_HIL_I2C_SPEED_POLICY default_hz=100000 safe_hz=100000 fast_hz=400000 fast_requires_logic_analyzer=1
EV_HIL_I2C_CASE_BEGIN name=sda-stuck-low-containment
EV_HIL_I2C_SDA_FORCE_LOW requested=1 observed=1
EV_HIL_I2C_BUS_STATE before=idle during=stuck after=recovered
EV_HIL_I2C_RECOVERY_BEGIN
EV_HIL_I2C_RECOVERY_RESULT status=PASS
EV_HIL_I2C_RECOVERY_EVIDENCE case=sda-stuck-low-containment pulses=9 stop_attempted=1 sda=1 scl=1 recovery_count_delta=1 post_recovery_probe=ACK
EV_HIL_I2C_CLOCK_STRETCH_EVIDENCE case=scl-held-low-timeout max_wait_us=300 timeout_status=TIMEOUT recovery_status=OK scl_held_low_case=PASS
EV_HIL_I2C_ACK_EVIDENCE name=read-stream-completion address_read_acks=1
EV_HIL_I2C_NACK_EVIDENCE name=missing-device-write-nack-stop-release address_write_nacks=1
EV_HIL_I2C_NACK_EVIDENCE name=missing-device-read-nack-stop-release address_read_nacks=1
EV_HIL_I2C_FINAL_NACK_SENT name=read-stream-completion count=1
EV_HIL_I2C_STOP_RELEASE name=read-stream-completion attempted=1 ok=1 fail=0 idle_ok=1 idle_fail=0 sda=1 scl=1 last_status=OK phase=10
EV_HIL_I2C_MUTEX_EVIDENCE name=read-stream-completion transaction_lock_count_delta=1 transaction_unlock_count_delta=1 unbalanced=0
EV_HIL_I2C_STOP_RELEASE name=missing-device-write-nack-stop-release attempted=1 ok=1 fail=0 idle_ok=1 idle_fail=0 sda=1 scl=1 last_status=NACK phase=10
EV_HIL_I2C_STOP_RELEASE name=missing-device-read-nack-stop-release attempted=1 ok=1 fail=0 idle_ok=1 idle_fail=0 sda=1 scl=1 last_status=NACK phase=10
EV_HIL_I2C_SCAN_NACK_POLICY addr7=0x7E status=NACK recovery_delta=0 stop_release_ok=1
EV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS
EV_HIL_RESULT PASS failures=0 skipped=0
"""
    assert parse_text(valid)["status"] == "PASS"
    assert parse_text(valid.replace("EV_HIL_RESULT PASS failures=0 skipped=0", ""))["status"] == "FAIL"
    assert parse_text(valid.replace("observed=1", "observed=0"))["status"] == "FAIL"
    assert "<REDACTED>" in redact("WIFI_PASSWORD=secret")
    assert "real-ssid" not in redact("I (7223) wifi:connected with real-ssid, aid = 23")
    assert safe_read_log(Path("/path/atnel-i2c.log"))[0] == "ENVIRONMENT_BLOCKED"
    print("ATNEL_I2C_HIL_LOG_PARSER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", type=Path)
    ap.add_argument("--evidence-dir", type=Path, default=DEFAULT_EVIDENCE)
    ap.add_argument("--environment-blocked", action="store_true")
    ap.add_argument("--check-only", action="store_true", help="parse or classify without writing release evidence")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.check_only and (args.environment_blocked or args.log is None):
        print("EV_HIL_ATNEL_I2C ENVIRONMENT_BLOCKED reason=ATNEL I2C hardware fixture or serial log is not available")
        return 77
    if args.environment_blocked or args.log is None:
        return environment_blocked("ATNEL I2C hardware fixture or serial log is not available", args.evidence_dir)
    status, reason, text = safe_read_log(args.log)
    if status == "ENVIRONMENT_BLOCKED":
        if args.check_only:
            print(f"EV_HIL_ATNEL_I2C ENVIRONMENT_BLOCKED reason={reason}")
            return 77
        return environment_blocked(reason, args.evidence_dir)
    if status == "FAIL":
        print(f"EV_HIL_ATNEL_I2C FAIL reason={reason}", file=sys.stderr)
        return 1
    if args.check_only:
        parsed = parse_text(text)
        print(f"EV_HIL_ATNEL_I2C CHECK_ONLY status={parsed['status']}")
        return 0 if parsed["status"] == "PASS" else 1
    return write_evidence(text, args.evidence_dir, args.log)


if __name__ == "__main__":
    raise SystemExit(main())
