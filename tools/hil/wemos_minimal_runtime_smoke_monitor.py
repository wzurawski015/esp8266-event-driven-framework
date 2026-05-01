#!/usr/bin/env python3
"""Serial smoke monitor for Wemos ESP-WROOM-02 18650 minimal runtime."""
from __future__ import annotations

import argparse
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md"
BOOT_RE = re.compile(r"EV_WEMOS_SMOKE_BOOT")
READY_RE = re.compile(r"EV_WEMOS_SMOKE_RUNTIME_READY")
FAIL_RE = re.compile(r"EV_WEMOS_SMOKE_FAIL|abort\(|Guru Meditation|Exception")
DIAG_TICK_RE = re.compile(r"ev_wroom02:\s+diag actor:\s+tick=([0-9]+)\b")
APP_SNAPSHOT_RE = re.compile(r"ev_wroom02:\s+app actor:\s+snapshot seq=([0-9]+)\b")
RESET_RE = re.compile(r"rst cause:|boot mode:|ets Jan|load 0x", re.IGNORECASE)
MIN_RUNTIME_ALIVE_SAMPLES = 3


@dataclass
class SmokeState:
    seen_boot: bool = False
    seen_ready: bool = False
    failed: bool = False
    fail_reason: str = ""
    reset_lines: int = 0
    diag_ticks: list[int] = field(default_factory=list)
    app_snapshots: list[int] = field(default_factory=list)


def _append_if_new_increasing(values: list[int], value: int) -> None:
    if not values or value > values[-1]:
        values.append(value)


def update_state_from_line(state: SmokeState, line: str) -> None:
    if FAIL_RE.search(line):
        state.failed = True
        state.fail_reason = "failure marker observed"
    if RESET_RE.search(line):
        state.reset_lines += 1
    state.seen_boot = state.seen_boot or bool(BOOT_RE.search(line))
    state.seen_ready = state.seen_ready or bool(READY_RE.search(line))
    diag = DIAG_TICK_RE.search(line)
    if diag:
        _append_if_new_increasing(state.diag_ticks, int(diag.group(1)))
    snapshot = APP_SNAPSHOT_RE.search(line)
    if snapshot:
        _append_if_new_increasing(state.app_snapshots, int(snapshot.group(1)))


def state_is_pass_marker_based(state: SmokeState) -> bool:
    return (not state.failed) and state.seen_boot and state.seen_ready


def state_is_pass_runtime_alive_fallback(state: SmokeState) -> bool:
    return (
        not state.failed
        and state.reset_lines == 0
        and len(state.diag_ticks) >= MIN_RUNTIME_ALIVE_SAMPLES
        and len(state.app_snapshots) >= MIN_RUNTIME_ALIVE_SAMPLES
    )


def write_report(status: str, reason: str, log_path: str = "", mode: str = "") -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        "# Wemos ESP-WROOM-02 18650 smoke report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Mode | {mode} |\n"
        f"| Log path | `{log_path}` |\n\n"
        "Preferred PASS requires `EV_WEMOS_SMOKE_BOOT` and `EV_WEMOS_SMOKE_RUNTIME_READY` serial markers. "
        "A fallback PASS is allowed only when the monitor observes at least three increasing "
        "`diag actor: tick=` values and at least three increasing `app actor: snapshot seq=` values without reset/failure markers.\n",
        encoding="utf-8",
    )


def not_run(reason: str) -> int:
    write_report("NOT_RUN", reason)
    print("EV_WEMOS_SMOKE_RESULT NOT_RUN")
    return 0


def monitor(port: str, baud: int, timeout_s: int) -> int:
    try:
        import serial  # type: ignore
    except Exception as exc:
        write_report("ENVIRONMENT_BLOCKED", f"pyserial import failed: {exc}")
        print(f"error: pyserial import failed: {exc}", file=sys.stderr)
        return 1
    log_dir = ROOT / "logs" / "hil" / "wemos"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"wemos_smoke_{int(time.time())}.log"
    deadline = time.monotonic() + timeout_s
    state = SmokeState()
    rel_log_path = ""
    with serial.Serial(port, baudrate=baud, timeout=1.0) as ser, log_path.open("w", encoding="utf-8") as log:
        rel_log_path = str(log_path.relative_to(ROOT))
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line)
            log.write(line + "\n")
            update_state_from_line(state, line)
            if state.failed:
                write_report("FAIL", state.fail_reason, rel_log_path, "failure-marker")
                print("EV_WEMOS_SMOKE_RESULT FAIL reason=failure_marker", file=sys.stderr)
                return 1
            if state_is_pass_marker_based(state):
                write_report("PASS", "required smoke markers observed", rel_log_path, "markers")
                print("EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=markers")
                return 0
            if state_is_pass_runtime_alive_fallback(state):
                write_report("PASS", "runtime-alive fallback observed increasing diag ticks and app snapshots", rel_log_path, "runtime_alive_fallback")
                print("EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=runtime_alive_fallback")
                return 0
    write_report("FAIL", "timed out waiting for smoke markers or runtime-alive fallback", rel_log_path, "timeout")
    print("EV_WEMOS_SMOKE_RESULT FAIL reason=timeout", file=sys.stderr)
    return 1


def self_test() -> int:
    marker_state = SmokeState()
    update_state_from_line(marker_state, "I (10) ev_wroom02: EV_WEMOS_SMOKE_BOOT board=wemos")
    update_state_from_line(marker_state, "I (20) ev_wroom02: EV_WEMOS_SMOKE_RUNTIME_READY")
    assert state_is_pass_marker_based(marker_state)

    fallback_state = SmokeState()
    sample_lines = [
        "I (2268) ev_wroom02: diag actor: tick=2 mono_now_ms=2268",
        "I (2273) ev_wroom02: app actor: snapshot seq=3 diag_ticks=2 last_tick_ms=2268",
        "I (3268) ev_wroom02: diag actor: tick=3 mono_now_ms=3268",
        "I (3273) ev_wroom02: app actor: snapshot seq=4 diag_ticks=3 last_tick_ms=3268",
        "I (4268) ev_wroom02: diag actor: tick=4 mono_now_ms=4268",
        "I (4273) ev_wroom02: app actor: snapshot seq=5 diag_ticks=4 last_tick_ms=4268",
    ]
    for line in sample_lines:
        update_state_from_line(fallback_state, line)
    assert state_is_pass_runtime_alive_fallback(fallback_state)

    failing_state = SmokeState()
    for line in sample_lines:
        update_state_from_line(failing_state, line)
    update_state_from_line(failing_state, "Guru Meditation")
    assert failing_state.failed
    assert not state_is_pass_runtime_alive_fallback(failing_state)

    reset_state = SmokeState()
    for line in sample_lines:
        update_state_from_line(reset_state, line)
    update_state_from_line(reset_state, "ets Jan  8 2013,rst cause:2, boot mode:(3,6)")
    assert not state_is_pass_runtime_alive_fallback(reset_state)

    print("EV_WEMOS_SMOKE_MONITOR_SELF_TEST PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?")
    parser.add_argument("baud", nargs="?", type=int, default=115200)
    parser.add_argument("timeout_s", nargs="?", type=int, default=120)
    parser.add_argument("--not-run-report", action="store_true")
    parser.add_argument("--reason", default="Wemos board was not attached or smoke was not requested")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.not_run_report:
        return not_run(args.reason)
    if not args.port:
        return not_run("serial port not provided")
    return monitor(args.port, args.baud, args.timeout_s)

if __name__ == "__main__":
    sys.exit(main())
