#!/usr/bin/env python3
"""Parse Wemos smoke and deep-sleep/wake serial evidence."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SMOKE_REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md"
DEEPSLEEP_REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_deep_sleep_wake_report.md"
SMOKE_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "wemos_smoke" / "current"
DEEPSLEEP_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "wemos_deepsleep" / "current"
BOOT_RE = re.compile(r"EV_WEMOS_SMOKE_BOOT")
READY_RE = re.compile(r"EV_WEMOS_SMOKE_RUNTIME_READY")
TICK_RE = re.compile(r"EV_WEMOS_SMOKE_TICK\s+seq=([0-9]+)")
SNAP_RE = re.compile(r"EV_WEMOS_SMOKE_SNAPSHOT\s+seq=([0-9]+)")
RESET_FAIL_RE = re.compile(r"EV_WEMOS_SMOKE_RESULT FAIL|panic|wdt reset|exception", re.IGNORECASE)
STATE_ORDER = [
    "ACTIVE",
    "SLEEP_REQUESTED",
    "DRAINING_RUNTIME",
    "LOG_FLUSHING",
    "PORTS_PREPARE_SLEEP",
    "RTC_STATE_SAVED",
    "ENTERING_DEEP_SLEEP",
]
STATE_RE = re.compile(r"EV_POWER_SMOKE_STATE\s+([A-Z_]+)")
DEEP_ENTER_RE = re.compile(r"EV_POWER_SMOKE_DEEP_SLEEP_ENTER")
SLEEP_REQUEST_RE = re.compile(r"EV_POWER_SMOKE_SLEEP_REQUEST\s+duration_us=([0-9]+)")
WAKE_BOOT_RE = re.compile(r"EV_POWER_SMOKE_WAKE_BOOT")
WAKE_REASON_RE = re.compile(r"EV_POWER_SMOKE_WAKE_REASON\s+reason=([^\s]+)")
POWER_RESULT_RE = re.compile(r"EV_POWER_SMOKE_RESULT\s+PASS")
SMOKE_RESULT_RE = re.compile(r"EV_WEMOS_SMOKE_RESULT\s+PASS")
SECRET_RE = re.compile(r"(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*")


def redact(text: str) -> str:
    return SECRET_RE.sub(lambda m: m.group(1) + "=<REDACTED>", text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def monotonic(values: list[int]) -> bool:
    return all(b > a for a, b in zip(values, values[1:]))


def parse_text(text: str, *, require_deepsleep: bool) -> dict[str, object]:
    text = redact(text)
    ticks = [int(m.group(1), 10) for m in TICK_RE.finditer(text)]
    snaps = [int(m.group(1), 10) for m in SNAP_RE.finditer(text)]
    states = [m.group(1) for m in STATE_RE.finditer(text)]
    failures: list[str] = []
    if not BOOT_RE.search(text):
        failures.append("missing boot marker")
    if not READY_RE.search(text):
        failures.append("missing runtime-ready marker")
    if len(ticks) < 3:
        failures.append("fewer than 3 tick markers")
    if len(snaps) < 3:
        failures.append("fewer than 3 snapshot markers")
    if not SMOKE_RESULT_RE.search(text):
        failures.append("missing EV_WEMOS_SMOKE_RESULT PASS")
    if not monotonic(ticks):
        failures.append("tick sequence is not strictly increasing")
    if not monotonic(snaps):
        failures.append("snapshot sequence is not strictly increasing")
    if RESET_FAIL_RE.search(text):
        failures.append("reset/failure marker observed")
    if require_deepsleep:
        if not SLEEP_REQUEST_RE.search(text):
            failures.append("missing EV_POWER_SMOKE_SLEEP_REQUEST duration_us marker")
        cursor = 0
        for expected in STATE_ORDER:
            try:
                cursor = states.index(expected, cursor) + 1
            except ValueError:
                failures.append(f"missing or out-of-order state {expected}")
                break
        if not DEEP_ENTER_RE.search(text):
            failures.append("missing deep-sleep enter marker")
        if not WAKE_BOOT_RE.search(text):
            failures.append("missing wake-boot marker")
        if not WAKE_REASON_RE.search(text):
            failures.append("missing EV_POWER_SMOKE_WAKE_REASON marker")
        if not POWER_RESULT_RE.search(text):
            failures.append("missing EV_POWER_SMOKE_RESULT PASS")
    return {
        "status": "PASS" if not failures else "FAIL",
        "mode": "deepsleep" if require_deepsleep else "smoke",
        "tick_count": len(ticks),
        "snapshot_count": len(snaps),
        "states": states,
        "failures": failures,
    }


def write_report(parsed: dict[str, object], evidence_dir: Path, report: Path, serial: Path | None) -> None:
    status = str(parsed.get("status", "FAIL"))
    reason = "all required markers observed" if status == "PASS" else "; ".join(parsed.get("failures", []))
    log_rel = serial.relative_to(ROOT).as_posix() if serial and serial.is_file() else ""
    parsed_rel = (evidence_dir / "parsed.json").relative_to(ROOT).as_posix()
    report.write_text(
        "# Wemos smoke/deep-sleep evidence report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Mode | {parsed.get('mode','unknown')} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_rel}` |\n"
        f"| Parsed evidence | `{parsed_rel}` |\n\n"
        "PASS requires deterministic boot/runtime/tick markers; deep-sleep mode also requires ordered power state markers and wake boot.\n",
        encoding="utf-8",
    )


def write_evidence(text: str, evidence_dir: Path, *, require_deepsleep: bool) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    serial = evidence_dir / "serial.log"
    serial.write_text(redact(text), encoding="utf-8")
    parsed = parse_text(text, require_deepsleep=require_deepsleep)
    parsed.update({"serial_log": serial.relative_to(ROOT).as_posix(), "serial_sha256": sha256(serial)})
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "excerpt.md").write_text("# Wemos evidence excerpt\n\n```text\n" + serial.read_text()[-4000:] + "\n```\n", encoding="utf-8")
    (evidence_dir / "sha256sums.txt").write_text(
        f"{sha256(serial)}  serial.log\n{sha256(evidence_dir / 'parsed.json')}  parsed.json\n",
        encoding="utf-8",
    )
    if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
        write_report(parsed, evidence_dir, DEEPSLEEP_REPORT if require_deepsleep else SMOKE_REPORT, serial)
    return 0 if parsed["status"] == "PASS" else 1


def environment_blocked(*, require_deepsleep: bool) -> int:
    evidence_dir = DEEPSLEEP_EVIDENCE if require_deepsleep else SMOKE_EVIDENCE
    evidence_dir.mkdir(parents=True, exist_ok=True)
    parsed = {"status": "ENVIRONMENT_BLOCKED", "mode": "deepsleep" if require_deepsleep else "smoke", "failures": ["Wemos hardware or serial log is not available"]}
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    report = DEEPSLEEP_REPORT if require_deepsleep else SMOKE_REPORT
    report.write_text(
        "# Wemos smoke/deep-sleep evidence report\n\n"
        "| Field | Value |\n|---|---|\n"
        "| Status | ENVIRONMENT_BLOCKED |\n"
        f"| Mode | {parsed['mode']} |\n"
        "| Reason | Wemos hardware or serial log is not available in this environment. |\n"
        f"| Parsed evidence | `{(evidence_dir / 'parsed.json').relative_to(ROOT).as_posix()}` |\n\n"
        "No PASS is declared without real serial evidence.\n",
        encoding="utf-8",
    )
    print(f"EV_WEMOS_EVIDENCE ENVIRONMENT_BLOCKED mode={parsed['mode']}")
    return 77


def self_test() -> int:
    smoke = """
EV_WEMOS_SMOKE_BOOT target=wemos_esp_wroom_02_18650
EV_WEMOS_SMOKE_RUNTIME_READY
EV_WEMOS_SMOKE_TICK seq=1
EV_WEMOS_SMOKE_SNAPSHOT seq=1
EV_WEMOS_SMOKE_TICK seq=2
EV_WEMOS_SMOKE_SNAPSHOT seq=2
EV_WEMOS_SMOKE_TICK seq=3
EV_WEMOS_SMOKE_SNAPSHOT seq=3
EV_WEMOS_SMOKE_RESULT PASS
"""
    assert parse_text(smoke, require_deepsleep=False)["status"] == "PASS"
    deep = smoke + "EV_POWER_SMOKE_SLEEP_REQUEST duration_us=1000000\n" + "\n".join(f"EV_POWER_SMOKE_STATE {state}" for state in STATE_ORDER) + "\nEV_POWER_SMOKE_DEEP_SLEEP_ENTER\nEV_POWER_SMOKE_WAKE_BOOT\nEV_POWER_SMOKE_WAKE_REASON reason=timer\nEV_POWER_SMOKE_RESULT PASS\n"
    assert parse_text(deep, require_deepsleep=True)["status"] == "PASS"
    assert parse_text(smoke.replace("seq=2", "seq=1"), require_deepsleep=False)["status"] == "FAIL"
    assert parse_text(deep.replace("EV_POWER_SMOKE_WAKE_BOOT", ""), require_deepsleep=True)["status"] == "FAIL"
    assert "<REDACTED>" in redact("COMMAND_TOKEN=secret")
    print("WEMOS_SMOKE_LOG_PARSER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", type=Path)
    ap.add_argument("--deepsleep", action="store_true")
    ap.add_argument("--environment-blocked", action="store_true")
    ap.add_argument("--evidence-dir", type=Path)
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.environment_blocked or args.log is None:
        return environment_blocked(require_deepsleep=args.deepsleep)
    return write_evidence(args.log.read_text(encoding="utf-8", errors="ignore"), args.evidence_dir or (DEEPSLEEP_EVIDENCE if args.deepsleep else SMOKE_EVIDENCE), require_deepsleep=args.deepsleep)

if __name__ == "__main__":
    raise SystemExit(main())
