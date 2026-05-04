#!/usr/bin/env python3
"""HIL release runner and report generator."""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PASS_RE = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0")
FAIL_RE = re.compile(r"EV_HIL_RESULT FAIL|EV_HIL_CASE .* FAIL")
CASES = {
    "atnel-i2c": {"report": "docs/release/hil_atnel_i2c_report.md", "log_dir": "logs/hil/i2c", "command": ["./tools/fw", "hil-i2c-run"], "title": "ATNEL I2C HIL"},
    "atnel-onewire": {"report": "docs/release/hil_atnel_onewire_report.md", "log_dir": "logs/hil/onewire", "command": ["./tools/fw", "hil-onewire-run"], "title": "ATNEL OneWire HIL"},
    "atnel-wifi": {"report": "docs/release/hil_atnel_wifi_report.md", "log_dir": "logs/hil/wifi", "command": ["./tools/fw", "hil-wifi-run"], "title": "ATNEL WiFi HIL"},
}


def write_report(case: str, status: str, reason: str, log_path: str = "") -> None:
    meta = CASES[case]
    report = ROOT / meta["report"]
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(
        f"# {meta['title']} report\n\n"
        f"| Field | Value |\n|---|---|\n"
        f"| Status | {status} |\n"
        f"| Reason | {reason} |\n"
        f"| Log path | `{log_path}` |\n\n"
        "PASS requires the serial marker `EV_HIL_RESULT PASS failures=0 skipped=0`.\n",
        encoding="utf-8",
    )


def run_case(case: str, mode: str) -> int:
    if case not in CASES:
        print(f"error: unknown HIL case: {case}", file=sys.stderr)
        return 2
    if mode == "not-run":
        write_report(case, "NOT_RUN", "hardware fixture was not attached or HIL was not requested")
        print(f"EV_HIL_RELEASE {case} NOT_RUN")
        return 0
    if mode == "dry-run":
        write_report(case, "NOT_RUN", "dry-run only; no hardware access attempted")
        print(json.dumps({"case": case, "mode": mode, "command": CASES[case]["command"]}))
        return 0
    meta = CASES[case]
    log_dir = ROOT / meta["log_dir"]
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    log_path = log_dir / f"{stamp}.log"
    with log_path.open("w", encoding="utf-8") as log:
        proc = subprocess.run(meta["command"], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, text=True)
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    rel_log = str(log_path.relative_to(ROOT))
    if PASS_RE.search(text) and proc.returncode == 0:
        write_report(case, "PASS", "monitor PASS marker observed", rel_log)
        return 0
    if FAIL_RE.search(text):
        write_report(case, "FAIL", "monitor FAIL marker observed", rel_log)
        return 1
    status = "FAIL" if proc.returncode != 0 else "FAIL"
    write_report(case, status, "no required PASS marker observed", rel_log)
    return 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Run or report ESP8266 HIL release cases")
    parser.add_argument("--case", choices=sorted(CASES) + ["all"], required=True)
    parser.add_argument("--mode", choices=["run", "dry-run", "not-run"], default="run")
    args = parser.parse_args()
    cases = sorted(CASES) if args.case == "all" else [args.case]
    rc = 0
    for case in cases:
        rc = max(rc, run_case(case, args.mode))
    return rc

if __name__ == "__main__":
    sys.exit(main())
