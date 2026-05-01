#!/usr/bin/env python3
"""Serial smoke monitor for Wemos ESP-WROOM-02 18650 minimal runtime."""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
REPORT = ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md"
BOOT_RE = re.compile(r"EV_WEMOS_SMOKE_BOOT")
READY_RE = re.compile(r"EV_WEMOS_SMOKE_RUNTIME_READY")
FAIL_RE = re.compile(r"EV_WEMOS_SMOKE_FAIL|abort\(|Guru Meditation|Exception")


def write_report(status: str, reason: str, log_path: str = "") -> None:
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(
        "# Wemos ESP-WROOM-02 18650 smoke report\n\n"
        "| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_path}` |\n\n"
        "PASS requires `EV_WEMOS_SMOKE_BOOT` and `EV_WEMOS_SMOKE_RUNTIME_READY` serial markers.\n",
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
    seen_boot = False
    seen_ready = False
    deadline = time.monotonic() + timeout_s
    with serial.Serial(port, baudrate=baud, timeout=1.0) as ser, log_path.open("w", encoding="utf-8") as log:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip()
            print(line)
            log.write(line + "\n")
            if FAIL_RE.search(line):
                write_report("FAIL", "failure marker observed", str(log_path.relative_to(ROOT)))
                return 1
            seen_boot = seen_boot or bool(BOOT_RE.search(line))
            seen_ready = seen_ready or bool(READY_RE.search(line))
            if seen_boot and seen_ready:
                write_report("PASS", "required smoke markers observed", str(log_path.relative_to(ROOT)))
                print("EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0")
                return 0
    write_report("FAIL", "timed out waiting for required smoke markers", str(log_path.relative_to(ROOT)))
    print("EV_WEMOS_SMOKE_RESULT FAIL reason=timeout", file=sys.stderr)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?")
    parser.add_argument("baud", nargs="?", type=int, default=115200)
    parser.add_argument("timeout_s", nargs="?", type=int, default=120)
    parser.add_argument("--not-run-report", action="store_true")
    parser.add_argument("--reason", default="Wemos board was not attached or smoke was not requested")
    args = parser.parse_args()
    if args.not_run_report:
        return not_run(args.reason)
    if not args.port:
        return not_run("serial port not provided")
    return monitor(args.port, args.baud, args.timeout_s)

if __name__ == "__main__":
    sys.exit(main())
