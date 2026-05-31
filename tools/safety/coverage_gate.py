#!/usr/bin/env python3
"""gcov-based host coverage gate for critical portable files."""
from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUDGET = ROOT / "config" / "coverage_budgets.json"
REPORT = ROOT / "docs" / "release" / "coverage_gate_report.md"
CRITICAL = [
    "core/src/ev_msg.c",
    "core/src/ev_mailbox.c",
    "core/src/ev_lease_pool.c",
    "runtime/src/ev_power_state_machine.c",
    "runtime/src/ev_qos_contract.c",
]
LINE_RE = re.compile(r"Lines executed:([0-9.]+)% of ([0-9]+)")


def run_gcov(rel: str) -> tuple[str, float, int]:
    gcov = shutil.which("gcov")
    if not gcov:
        raise EnvironmentError("gcov not found")
    proc = subprocess.run([gcov, "-o", "build/host-coverage/obj/" + str(Path(rel).parent), rel], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pct = 0.0
    lines = 0
    for line in proc.stdout.splitlines():
        m = LINE_RE.search(line)
        if m:
            pct = float(m.group(1))
            lines = int(m.group(2))
            break
    return proc.stdout, pct, lines


def load_budget() -> dict:
    return json.loads(BUDGET.read_text(encoding="utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--report-only", action="store_true")
    args = ap.parse_args()
    try:
        budget = load_budget()
        min_file = float(budget.get("critical_file_min_line_percent", 20.0))
        allow = set(budget.get("coverage_exceptions", {}).keys())
        rows = []
        errors = []
        for rel in CRITICAL:
            out, pct, lines = run_gcov(rel)
            status = "PASS" if pct >= min_file or rel in allow else "FAIL"
            if status == "FAIL":
                errors.append(f"{rel}: {pct:.1f}% < {min_file:.1f}%")
            rows.append((rel, pct, lines, status))
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(render(rows, errors, min_file, budget), encoding="utf-8")
        print(f"EV_COVERAGE_REPORT {REPORT.relative_to(ROOT)}")
        if errors and not args.report_only:
            for e in errors:
                print(f"EV_COVERAGE_GATE FAIL {e}", file=sys.stderr)
            return 1
        print("EV_COVERAGE_GATE PASS" if not args.report_only else "EV_COVERAGE_REPORT_ONLY PASS")
        return 0
    except EnvironmentError as exc:
        REPORT.write_text("# Coverage gate report\n\nStatus: ENVIRONMENT_BLOCKED\n\nReason: " + str(exc) + "\n", encoding="utf-8")
        print(f"EV_COVERAGE_GATE ENVIRONMENT_BLOCKED reason={exc}")
        return 77


def render(rows: list[tuple[str, float, int, str]], errors: list[str], min_file: float, budget: dict) -> str:
    lines = ["# Coverage gate report", "", f"Minimum critical-file line coverage: {min_file:.1f}%", "", "| File | Lines | Coverage | Status |", "|---|---:|---:|---:|"]
    for rel, pct, count, status in rows:
        lines.append(f"| `{rel}` | {count} | {pct:.1f}% | {status} |")
    lines += ["", "Exceptions:"]
    for rel, reason in budget.get("coverage_exceptions", {}).items():
        lines.append(f"- `{rel}`: {reason}")
    if errors:
        lines += ["", "Failures:"] + [f"- {e}" for e in errors]
    lines += ["", "Coverage is host-only; SDK/HIL coverage remains outside this gate.", ""]
    return "\n".join(lines)

if __name__ == "__main__":
    raise SystemExit(main())
