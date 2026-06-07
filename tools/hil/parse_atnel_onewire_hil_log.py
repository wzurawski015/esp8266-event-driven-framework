#!/usr/bin/env python3
"""Parse ATNEL OneWire DS18B20 HIL logs with fail-closed evidence rules."""
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

REPORT = ROOT / "docs" / "release" / "hil_atnel_onewire_report.md"
DEFAULT_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "onewire" / "current"

GLOBAL_PASS = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0")
GLOBAL_FAIL = re.compile(r"EV_HIL_RESULT FAIL|EV_HIL_CASE\s+[^\s]+\s+FAIL")
CASE_PASS = re.compile(r"EV_HIL_CASE\s+ds18b20-read-irq-flood\s+PASS")
CRC_PASS = re.compile(r"EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC\s+name=ds18b20-read-irq-flood\s+iteration=\d+\s+status=PASS")
RELEASE_OK = re.compile(r"EV_HIL_ONEWIRE_RELEASE_EVIDENCE\s+name=ds18b20-read-irq-flood\s+dq=1\s+busy=0\s+bus_errors_delta=0")
STACK_MARKER = re.compile(r"EV_HIL_STACK\s+task=irq-flood\s+(?:high_water_words=\d+|status=not_available)")
IRQ_DIAG = re.compile(r"irq-diag:(?:before|after)")
ONEWIRE_DIAG = re.compile(r"onewire-diag:(?:before|after).*dq_high=([01])")
SUMMARY_OK = re.compile(r"HIL summary passed=\d+ failed=0 skipped=0")
PIN_MAP = re.compile(r"EV_HIL_ONEWIRE_PIN_MAP\s+board=\S+\s+dq_gpio=\d+\s+pullup=external\s+required=1.*wifi=on")
TIMING_MARKER = re.compile(r"EV_HIL_ONEWIRE_TIMING\s+.*reset_low_us=\d+.*slot_min_us=\d+.*slot_max_us=\d+")
TIMING_MARKER_WIFI_ON = re.compile(r"EV_HIL_ONEWIRE_TIMING\s+.*reset_low_us=\d+.*slot_min_us=\d+.*slot_max_us=\d+.*wifi=on")
WIFI_TIMING_PASS_ON = re.compile(r"EV_HIL_ONEWIRE_WIFI_TIMING\s+status=PASS\s+wifi=on")
WIFI_TIMING_BLOCKED = re.compile(r"EV_HIL_ONEWIRE_WIFI_TIMING\s+status=ENVIRONMENT_BLOCKED|EV_HIL_ONEWIRE_WIFI_TIMING\s+status=PASS\s+wifi=(?:off|blocked)")
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
    has_global_pass = bool(GLOBAL_PASS.search(redacted))
    has_global_fail = bool(GLOBAL_FAIL.search(redacted))
    has_case_pass = bool(CASE_PASS.search(redacted))
    has_crc_pass = bool(CRC_PASS.search(redacted))
    has_release_ok = bool(RELEASE_OK.search(redacted))
    has_stack_marker = bool(STACK_MARKER.search(redacted))
    has_irq_diag = len(IRQ_DIAG.findall(redacted)) >= 2
    has_onewire_diag = len(ONEWIRE_DIAG.findall(redacted)) >= 2
    summary_ok = bool(SUMMARY_OK.search(redacted))
    has_pin_map = bool(PIN_MAP.search(redacted))
    has_timing_marker = bool(TIMING_MARKER.search(redacted))
    has_timing_marker_wifi_on = bool(TIMING_MARKER_WIFI_ON.search(redacted))
    has_wifi_timing_pass_on = bool(WIFI_TIMING_PASS_ON.search(redacted))
    has_wifi_timing_blocked = bool(WIFI_TIMING_BLOCKED.search(redacted))
    failures: list[str] = []
    if has_global_fail:
        failures.append("failure marker observed")
    if not has_global_pass:
        failures.append("missing EV_HIL_RESULT PASS failures=0 skipped=0")
    if not summary_ok:
        failures.append("missing HIL summary failed=0 skipped=0")
    if not has_pin_map:
        failures.append("missing EV_HIL_ONEWIRE_PIN_MAP marker")
    if not has_timing_marker:
        failures.append("missing EV_HIL_ONEWIRE_TIMING marker")
    if not has_timing_marker_wifi_on:
        failures.append("missing detailed EV_HIL_ONEWIRE_TIMING marker with wifi=on")
    if not has_wifi_timing_pass_on:
        failures.append("missing EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on")
    if has_wifi_timing_blocked:
        failures.append("WiFi-on timing evidence is ENVIRONMENT_BLOCKED")
    if not has_case_pass:
        failures.append("missing DS18B20 IRQ-flood case PASS marker")
    if not has_crc_pass:
        failures.append("missing DS18B20 scratchpad CRC PASS marker")
    if not has_release_ok:
        failures.append("missing DQ release/bus-idle evidence marker")
    if not has_stack_marker:
        failures.append("missing IRQ flood stack evidence marker")
    if not has_irq_diag:
        failures.append("missing before/after IRQ diagnostics")
    if not has_onewire_diag:
        failures.append("missing before/after OneWire diagnostics")
    return {
        "status": "PASS" if not failures else "FAIL",
        "case": "ds18b20-read-irq-flood",
        "global_pass": has_global_pass,
        "case_pass": has_case_pass,
        "scratchpad_crc_pass": has_crc_pass,
        "dq_release_ok": has_release_ok,
        "stack_marker": has_stack_marker,
        "irq_diag_before_after": has_irq_diag,
        "onewire_diag_before_after": has_onewire_diag,
        "summary_ok": summary_ok,
        "pin_map": has_pin_map,
        "timing_marker": has_timing_marker,
        "timing_marker_wifi_on": has_timing_marker_wifi_on,
        "wifi_timing_pass_on": has_wifi_timing_pass_on,
        "wifi_timing_blocked": has_wifi_timing_blocked,
        "failures": failures,
    }


def write_report(parsed: dict[str, object], evidence_dir: Path, serial_log: Path | None = None) -> None:
    status = str(parsed.get("status", "FAIL"))
    reason = "all required OneWire DS18B20 HIL markers observed" if status == "PASS" else "; ".join(parsed.get("failures", []))
    log_rel = serial_log.relative_to(ROOT).as_posix() if serial_log and serial_log.is_file() and serial_log.is_relative_to(ROOT) else ""
    parsed_rel = (evidence_dir / "parsed.json").relative_to(ROOT).as_posix() if (evidence_dir / "parsed.json").is_relative_to(ROOT) else str(evidence_dir / "parsed.json")
    REPORT.write_text(
        "# ATNEL OneWire HIL report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_rel}` |\n"
        f"| Parsed evidence | `{parsed_rel}` |\n\n"
        "PASS requires real serial evidence for DS18B20 scratchpad CRC, DQ release, "
        "IRQ flood stack evidence and before/after diagnostics.  Missing hardware is "
        "reported as ENVIRONMENT_BLOCKED, not PASS.\n",
        encoding="utf-8",
    )


def write_evidence(text: str, evidence_dir: Path, source: Path | None = None) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    serial = evidence_dir / "serial.log"
    serial.write_text(redact(text), encoding="utf-8")
    parsed = parse_text(text)
    parsed.update({"serial_log": serial.relative_to(ROOT).as_posix() if serial.is_relative_to(ROOT) else str(serial), "serial_sha256": sha256(serial)})
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "excerpt.md").write_text("# ATNEL OneWire HIL excerpt\n\n```text\n" + serial.read_text(encoding="utf-8")[-4000:] + "\n```\n", encoding="utf-8")
    (evidence_dir / "sha256sums.txt").write_text(
        f"{sha256(serial)}  serial.log\n{sha256(evidence_dir / 'parsed.json')}  parsed.json\n",
        encoding="utf-8",
    )
    if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
        write_report(parsed, evidence_dir, serial)
    return 0 if parsed["status"] == "PASS" else 1


def environment_blocked(reason: str, evidence_dir: Path = DEFAULT_EVIDENCE) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    parsed = {"status": "ENVIRONMENT_BLOCKED", "case": "ds18b20-read-irq-flood", "reason": reason, "failures": [reason]}
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
        REPORT.write_text(
            "# ATNEL OneWire HIL report\n\n"
            "| Field | Value |\n|---|---|\n"
            "| Status | ENVIRONMENT_BLOCKED |\n"
            f"| Reason | {reason} |\n"
            f"| Parsed evidence | `{(evidence_dir / 'parsed.json').relative_to(ROOT).as_posix()}` |\n\n"
            "No PASS is declared without real serial evidence.\n",
            encoding="utf-8",
        )
    print(f"EV_HIL_ATNEL_ONEWIRE ENVIRONMENT_BLOCKED reason={reason}")
    return 77


def self_test() -> int:
    valid = """
irq-diag:before write=0 read=0 pending=0 dropped=0 high_watermark=0 mask=0x00000000
onewire-diag:before ops=0 crit=0 reset_crit=0 bit_crit=0 max_us=0 reset_max_us=0 bit_max_us=0 budget_violations=0 reset_low_us=0 bus_errors=0 busy=0 dq_high=1
EV_HIL_ONEWIRE_PIN_MAP board=ev_atnel dq_gpio=12 pullup=external required=1 source=bsp/atnel_air_esp_motherboard/pins.def wifi=on
EV_HIL_STACK task=irq-flood high_water_words=123
EV_HIL_ONEWIRE_TIMING reset_low_us=500 reset_high_us=480 presence_low_us=120 slot_min_us=60 slot_max_us=80 recovery_min_us=2 wifi=on
EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on
EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC name=ds18b20-read-irq-flood iteration=0 status=PASS
EV_HIL_ONEWIRE_RELEASE_EVIDENCE name=ds18b20-read-irq-flood dq=1 busy=0 bus_errors_delta=0 max_crit_us=70 reset_max_us=600 bit_max_us=10
EV_HIL_CASE ds18b20-read-irq-flood PASS
irq-diag:after write=4 read=4 pending=0 dropped=0 high_watermark=2 mask=0x00000001
onewire-diag:after ops=4 crit=32 reset_crit=4 bit_crit=28 max_us=70 reset_max_us=600 bit_max_us=10 budget_violations=0 reset_low_us=500 bus_errors=0 busy=0 dq_high=1
HIL summary passed=1 failed=0 skipped=0
EV_HIL_RESULT PASS failures=0 skipped=0
"""
    assert parse_text(valid)["status"] == "PASS"
    assert parse_text(valid.replace("EV_HIL_ONEWIRE_RELEASE_EVIDENCE", "MISSING_RELEASE"))["status"] == "FAIL"
    assert parse_text(valid.replace("EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on", "MISSING_WIFI_TIMING_PASS"))["status"] == "FAIL"
    assert parse_text(valid.replace("EV_HIL_ONEWIRE_TIMING reset_low_us=500 reset_high_us=480 presence_low_us=120 slot_min_us=60 slot_max_us=80 recovery_min_us=2 wifi=on", "EV_HIL_ONEWIRE_TIMING reset_low_us=500 reset_high_us=480 presence_low_us=120 slot_min_us=60 slot_max_us=80 recovery_min_us=2 wifi=off"))["status"] == "FAIL"
    assert parse_text(valid.replace("EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on", "EV_HIL_ONEWIRE_WIFI_TIMING status=ENVIRONMENT_BLOCKED wifi=blocked"))["status"] == "FAIL"
    assert parse_text(valid.replace("wifi=on", "wifi=off"))["status"] == "FAIL"
    assert parse_text(valid.replace("EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC name=ds18b20-read-irq-flood iteration=0 status=PASS", "EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC name=ds18b20-read-irq-flood iteration=0 status=FAIL"))["status"] == "FAIL"
    assert "real-ssid" not in redact("wifi:connected with real-ssid, aid = 1")
    assert safe_read_log(Path("/path/onewire.log"))[0] == "ENVIRONMENT_BLOCKED"
    print("ATNEL_ONEWIRE_HIL_LOG_PARSER_SELF_TEST PASS")
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
        print("EV_HIL_ATNEL_ONEWIRE ENVIRONMENT_BLOCKED reason=ATNEL OneWire DS18B20 HIL fixture or serial log is not available")
        return 77
    if args.environment_blocked or args.log is None:
        return environment_blocked("ATNEL OneWire DS18B20 HIL fixture or serial log is not available", args.evidence_dir)
    status, reason, text = safe_read_log(args.log)
    if status == "ENVIRONMENT_BLOCKED":
        if args.check_only:
            print(f"EV_HIL_ATNEL_ONEWIRE ENVIRONMENT_BLOCKED reason={reason}")
            return 77
        return environment_blocked(reason, args.evidence_dir)
    if status == "FAIL":
        print(f"EV_HIL_ATNEL_ONEWIRE FAIL reason={reason}", file=sys.stderr)
        return 1
    if args.check_only:
        parsed = parse_text(text)
        print(f"EV_HIL_ATNEL_ONEWIRE CHECK_ONLY status={parsed['status']}")
        return 0 if parsed["status"] == "PASS" else 1
    return write_evidence(text, args.evidence_dir, args.log)


if __name__ == "__main__":
    raise SystemExit(main())
