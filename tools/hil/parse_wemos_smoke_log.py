#!/usr/bin/env python3
"""Parse Wemos smoke and deep-sleep/wake serial evidence.

The default parser is strict: a normal smoke PASS requires explicit boot,
runtime-ready and result markers.  A separate late-attach mode can be enabled
for operator logs that start after early boot markers; that mode is marked in
parsed JSON as runtime_alive_fallback and is never accepted for deep-sleep PASS.
"""
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
sys.path.insert(0, str(ROOT / "tools" / "release"))
from operator_exit_footer import strip_footer

SMOKE_REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md"
DEEPSLEEP_REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_deep_sleep_wake_report.md"
SMOKE_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "wemos_smoke" / "current"
DEEPSLEEP_EVIDENCE = ROOT / "docs" / "release" / "hil_evidence" / "wemos_deepsleep" / "current"

BOOT_RE = re.compile(r"EV_WEMOS_SMOKE_BOOT")
READY_RE = re.compile(r"EV_WEMOS_SMOKE_RUNTIME_READY")
TICK_RE = re.compile(r"EV_WEMOS_SMOKE_TICK\s+seq=([0-9]+)")
SNAP_RE = re.compile(r"EV_WEMOS_SMOKE_SNAPSHOT\s+seq=([0-9]+)")
SMOKE_RESULT_PASS_RE = re.compile(r"EV_WEMOS_SMOKE_RESULT\s+PASS(?:\s+[^\n]*)?")
FIRMWARE_RUNTIME_ALIVE_RE = re.compile(r"EV_WEMOS_SMOKE_RESULT\s+PASS[^\n]*mode=firmware_runtime_alive")
RESET_FAIL_RE = re.compile(
    r"EV_WEMOS_SMOKE_RESULT\s+FAIL|\bpanic\b|\bfatal\b|\bexception\b|wdt\s+reset|watchdog|rst cause|Hard resetting via RTS pin|esptool\.py|abort\(",
    re.IGNORECASE,
)
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
SECRET_RE = re.compile(r"(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*")
PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)
MIN_RUNTIME_ALIVE_SAMPLES = 3


def redact(text: str) -> str:
    return redact_text(text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def rel_or_str(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


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


def monotonic(values: list[int]) -> bool:
    return all(b > a for a, b in zip(values, values[1:]))


def enough_runtime_alive(ticks: list[int], snaps: list[int]) -> bool:
    return (len(ticks) >= MIN_RUNTIME_ALIVE_SAMPLES) and (len(snaps) >= MIN_RUNTIME_ALIVE_SAMPLES) and monotonic(ticks) and monotonic(snaps)


def parse_text(text: str, *, require_deepsleep: bool, allow_runtime_alive_fallback: bool = False) -> dict[str, object]:
    redacted_text = redact(text)
    text, operator_footer, _operator_footer_text = strip_footer(redacted_text)
    ticks = [int(m.group(1), 10) for m in TICK_RE.finditer(text)]
    snaps = [int(m.group(1), 10) for m in SNAP_RE.finditer(text)]
    states = [m.group(1) for m in STATE_RE.finditer(text)]
    boot_seen = BOOT_RE.search(text) is not None
    ready_seen = READY_RE.search(text) is not None
    result_seen = SMOKE_RESULT_PASS_RE.search(text) is not None
    firmware_runtime_alive_seen = FIRMWARE_RUNTIME_ALIVE_RE.search(text) is not None
    reset_seen = RESET_FAIL_RE.search(text) is not None
    tick_monotonic = monotonic(ticks)
    snapshot_monotonic = monotonic(snaps)
    runtime_alive_ok = enough_runtime_alive(ticks, snaps) and not reset_seen

    failures: list[str] = []
    marker_based = boot_seen and ready_seen and result_seen and runtime_alive_ok
    runtime_alive_fallback = False
    fallback_reason = ""

    if require_deepsleep:
        if allow_runtime_alive_fallback:
            failures.append("runtime-alive fallback is not allowed for deep-sleep evidence")
        if not boot_seen:
            failures.append("missing boot marker")
        if not ready_seen:
            failures.append("missing runtime-ready marker")
        if len(ticks) < MIN_RUNTIME_ALIVE_SAMPLES:
            failures.append(f"fewer than {MIN_RUNTIME_ALIVE_SAMPLES} tick markers")
        if len(snaps) < MIN_RUNTIME_ALIVE_SAMPLES:
            failures.append(f"fewer than {MIN_RUNTIME_ALIVE_SAMPLES} snapshot markers")
        if not result_seen:
            failures.append("missing EV_WEMOS_SMOKE_RESULT PASS")
    else:
        if not marker_based:
            if allow_runtime_alive_fallback and runtime_alive_ok:
                runtime_alive_fallback = True
                fallback_reason = "late-attached serial log with monotonic tick/snapshot evidence"
                if firmware_runtime_alive_seen:
                    fallback_reason = "firmware runtime-alive result marker with monotonic tick/snapshot evidence"
            else:
                if not boot_seen:
                    failures.append("missing boot marker")
                if not ready_seen:
                    failures.append("missing runtime-ready marker")
                if len(ticks) < MIN_RUNTIME_ALIVE_SAMPLES:
                    failures.append(f"fewer than {MIN_RUNTIME_ALIVE_SAMPLES} tick markers")
                if len(snaps) < MIN_RUNTIME_ALIVE_SAMPLES:
                    failures.append(f"fewer than {MIN_RUNTIME_ALIVE_SAMPLES} snapshot markers")
                if not result_seen:
                    failures.append("missing EV_WEMOS_SMOKE_RESULT PASS")

    if not tick_monotonic:
        failures.append("tick sequence is not strictly increasing")
    if not snapshot_monotonic:
        failures.append("snapshot sequence is not strictly increasing")
    if reset_seen:
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

    status = "PASS" if not failures else "FAIL"
    mode = "deepsleep" if require_deepsleep else ("runtime_alive_fallback" if runtime_alive_fallback else "smoke")
    return {
        "status": status,
        "mode": mode,
        "marker_based": bool(marker_based and not runtime_alive_fallback),
        "runtime_alive_fallback": runtime_alive_fallback,
        "boot_marker_seen": boot_seen,
        "runtime_ready_marker_seen": ready_seen,
        "result_marker_seen": result_seen,
        "firmware_runtime_alive_result_seen": firmware_runtime_alive_seen,
        "tick_count": len(ticks),
        "snapshot_count": len(snaps),
        "tick_monotonic": tick_monotonic,
        "snapshot_monotonic": snapshot_monotonic,
        "states": states,
        "fallback_reason": fallback_reason,
        "failures": failures,
        "operator_interrupt_seen": bool(operator_footer.get("operator_interrupt_seen")),
        "operator_exit_code": operator_footer.get("operator_exit_code"),
        "operator_exit_signal": operator_footer.get("operator_exit_signal"),
        "operator_exit_classification": operator_footer.get("operator_exit_classification"),
    }


def normalized_text(raw_text: str, parsed: dict[str, object]) -> str:
    text, _operator_footer, _operator_footer_text = strip_footer(redact(raw_text))
    text = text.rstrip() + "\n"
    if parsed.get("status") == "PASS" and parsed.get("runtime_alive_fallback"):
        text += "EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=runtime_alive_fallback normalized_by=parse_wemos_smoke_log.py\n"
    return text


def write_report(parsed: dict[str, object], evidence_dir: Path, report: Path, serial: Path | None) -> None:
    status = str(parsed.get("status", "FAIL"))
    reason = "all required markers observed"
    if parsed.get("runtime_alive_fallback"):
        reason = str(parsed.get("fallback_reason", "runtime-alive fallback"))
    elif status != "PASS":
        reason = "; ".join(str(x) for x in parsed.get("failures", []))
    log_rel = rel_or_str(serial) if serial and serial.is_file() else ""
    parsed_path = evidence_dir / "parsed.json"
    parsed_rel = rel_or_str(parsed_path)
    report.write_text(
        "# Wemos smoke/deep-sleep evidence report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Mode | {parsed.get('mode','unknown')} |\n"
        f"| Marker based | {parsed.get('marker_based', False)} |\n"
        f"| Runtime-alive fallback | {parsed.get('runtime_alive_fallback', False)} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_rel}` |\n"
        f"| Parsed evidence | `{parsed_rel}` |\n\n"
        "Smoke PASS is either strict marker-based evidence or explicitly marked `runtime_alive_fallback`. Deep-sleep PASS remains strict and requires ordered power state markers plus wake evidence. Placeholder or missing paths are reported as ENVIRONMENT_BLOCKED, not Python tracebacks.\n",
        encoding="utf-8",
    )


def write_evidence(text: str, evidence_dir: Path, *, require_deepsleep: bool, allow_runtime_alive_fallback: bool = False, normalize: bool = False) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    redacted_text = redact(text)
    firmware_text, operator_footer, operator_footer_text = strip_footer(redacted_text)
    parsed = parse_text(text, require_deepsleep=require_deepsleep, allow_runtime_alive_fallback=allow_runtime_alive_fallback)
    if operator_footer_text:
        footer = evidence_dir / "operator_footer.log"
        footer.write_text(operator_footer_text, encoding="utf-8")
        parsed.update({
            "operator_footer_log": rel_or_str(footer),
            "operator_footer_sha256": sha256(footer),
        })

    if normalize:
        raw = evidence_dir / "serial.raw.log"
        norm = evidence_dir / "serial.normalized.log"
        raw.write_text(redacted_text, encoding="utf-8")
        norm.write_text(normalized_text(text, parsed), encoding="utf-8")
        serial = evidence_dir / "serial.log"
        serial.write_text(norm.read_text(encoding="utf-8"), encoding="utf-8")
        parsed.update({
            "serial_raw_log": rel_or_str(raw),
            "serial_raw_sha256": sha256(raw),
            "serial_normalized_log": rel_or_str(norm),
            "serial_normalized_sha256": sha256(norm),
            "serial_log": rel_or_str(serial),
            "serial_sha256": sha256(serial),
            "normalized": True,
        })
    else:
        serial = evidence_dir / "serial.log"
        serial.write_text(firmware_text, encoding="utf-8")
        parsed.update({"serial_log": rel_or_str(serial), "serial_sha256": sha256(serial), "normalized": False})

    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "excerpt.md").write_text("# Wemos evidence excerpt\n\n```text\n" + serial.read_text(encoding="utf-8")[-4000:] + "\n```\n", encoding="utf-8")
    sha_lines = [
        f"{sha256(serial)}  serial.log",
        f"{sha256(evidence_dir / 'parsed.json')}  parsed.json",
    ]
    if normalize:
        sha_lines.insert(0, f"{parsed['serial_raw_sha256']}  serial.raw.log")
        sha_lines.insert(1, f"{parsed['serial_normalized_sha256']}  serial.normalized.log")
    (evidence_dir / "sha256sums.txt").write_text("\n".join(sha_lines) + "\n", encoding="utf-8")

    try:
        if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
            write_report(parsed, evidence_dir, DEEPSLEEP_REPORT if require_deepsleep else SMOKE_REPORT, serial)
    except AttributeError:
        # Python < 3.9 fallback is unnecessary in current CI, but keep the code robust.
        pass
    return 0 if parsed["status"] == "PASS" else 1


def environment_blocked(*, require_deepsleep: bool, reason: str = "Wemos hardware or serial log is not available", evidence_dir: Path | None = None) -> int:
    evidence_dir = evidence_dir or (DEEPSLEEP_EVIDENCE if require_deepsleep else SMOKE_EVIDENCE)
    evidence_dir.mkdir(parents=True, exist_ok=True)
    parsed = {"status": "ENVIRONMENT_BLOCKED", "mode": "deepsleep" if require_deepsleep else "smoke", "reason": reason, "failures": [reason], "runtime_alive_fallback": False}
    (evidence_dir / "parsed.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    report = DEEPSLEEP_REPORT if require_deepsleep else SMOKE_REPORT
    try:
        if evidence_dir.resolve().is_relative_to((ROOT / "docs" / "release").resolve()):
            report.write_text(
                "# Wemos smoke/deep-sleep evidence report\n\n"
                "| Field | Value |\n|---|---|\n"
                "| Status | ENVIRONMENT_BLOCKED |\n"
                f"| Mode | {parsed['mode']} |\n"
                f"| Reason | {reason} |\n"
                f"| Parsed evidence | `{rel_or_str(evidence_dir / 'parsed.json')}` |\n\n"
                "No PASS is declared without real serial evidence.\n",
                encoding="utf-8",
            )
    except AttributeError:
        pass
    print(f"EV_WEMOS_EVIDENCE ENVIRONMENT_BLOCKED mode={parsed['mode']} reason={reason}")
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
    late = """
EV_WEMOS_SMOKE_TICK seq=8
EV_WEMOS_SMOKE_SNAPSHOT seq=8
EV_WEMOS_SMOKE_TICK seq=9
EV_WEMOS_SMOKE_SNAPSHOT seq=9
EV_WEMOS_SMOKE_TICK seq=10
EV_WEMOS_SMOKE_SNAPSHOT seq=10
"""
    assert parse_text(late, require_deepsleep=False)["status"] == "FAIL"
    late_pass = parse_text(late, require_deepsleep=False, allow_runtime_alive_fallback=True)
    assert late_pass["status"] == "PASS" and late_pass["runtime_alive_fallback"] is True
    assert parse_text(late.replace("seq=9", "seq=8"), require_deepsleep=False, allow_runtime_alive_fallback=True)["status"] == "FAIL"
    footer = "^C\n--- exit ---\n[process exited with code 130 (0x00000082)]\n"
    with_footer = parse_text(late + footer, require_deepsleep=False, allow_runtime_alive_fallback=True)
    assert with_footer["status"] == "PASS" and with_footer["operator_exit_code"] == 130
    assert with_footer["operator_exit_classification"] == "CONTROLLED_MONITOR_STOP"
    assert parse_text(footer, require_deepsleep=False, allow_runtime_alive_fallback=True)["status"] == "FAIL"
    assert parse_text(late + "panic\n" + footer, require_deepsleep=False, allow_runtime_alive_fallback=True)["status"] == "FAIL"
    firmware_alive = late + "EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=firmware_runtime_alive\n"
    fw = parse_text(firmware_alive, require_deepsleep=False, allow_runtime_alive_fallback=True)
    assert fw["status"] == "PASS" and fw["firmware_runtime_alive_result_seen"] is True
    deep = smoke + "EV_POWER_SMOKE_SLEEP_REQUEST duration_us=1000000\n" + "\n".join(f"EV_POWER_SMOKE_STATE {state}" for state in STATE_ORDER) + "\nEV_POWER_SMOKE_DEEP_SLEEP_ENTER\nEV_POWER_SMOKE_WAKE_BOOT\nEV_POWER_SMOKE_WAKE_REASON reason=timer\nEV_POWER_SMOKE_RESULT PASS\n"
    assert parse_text(deep, require_deepsleep=True)["status"] == "PASS"
    assert parse_text(late + "^C\n[process exited with code 130 (0x00000082)]\n", require_deepsleep=True, allow_runtime_alive_fallback=True)["status"] == "FAIL"
    assert parse_text(deep.replace("EV_POWER_SMOKE_WAKE_BOOT", ""), require_deepsleep=True)["status"] == "FAIL"
    assert "<REDACTED>" in redact("COMMAND_TOKEN=secret")
    sdk_wifi = "I (7223) wifi:connected with lab-ssid-value, aid = 23"
    assert "lab-ssid-value" not in redact(sdk_wifi)
    assert "wifi:connected with <REDACTED>, aid = 23" in redact(sdk_wifi)
    assert safe_read_log(Path("/path/wemos-smoke.log"))[0] == "ENVIRONMENT_BLOCKED"
    print("WEMOS_SMOKE_LOG_PARSER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", type=Path)
    ap.add_argument("--deepsleep", action="store_true")
    ap.add_argument("--allow-runtime-alive-fallback", action="store_true")
    ap.add_argument("--normalize", action="store_true")
    ap.add_argument("--environment-blocked", action="store_true")
    ap.add_argument("--evidence-dir", type=Path)
    args = ap.parse_args()
    evidence_dir = args.evidence_dir or (DEEPSLEEP_EVIDENCE if args.deepsleep else SMOKE_EVIDENCE)
    if args.self_test:
        return self_test()
    if args.deepsleep and args.allow_runtime_alive_fallback:
        print("EV_WEMOS_EVIDENCE FAIL reason=runtime-alive fallback is not allowed for deep-sleep evidence", file=sys.stderr)
        return 1
    if args.environment_blocked or args.log is None:
        return environment_blocked(require_deepsleep=args.deepsleep, evidence_dir=evidence_dir)
    status, reason, text = safe_read_log(args.log)
    if status == "ENVIRONMENT_BLOCKED":
        return environment_blocked(require_deepsleep=args.deepsleep, reason=reason, evidence_dir=evidence_dir)
    if status == "FAIL":
        print(f"EV_WEMOS_EVIDENCE FAIL reason={reason}", file=sys.stderr)
        return 1
    return write_evidence(
        text,
        evidence_dir,
        require_deepsleep=args.deepsleep,
        allow_runtime_alive_fallback=args.allow_runtime_alive_fallback,
        normalize=args.normalize,
    )


if __name__ == "__main__":
    raise SystemExit(main())
