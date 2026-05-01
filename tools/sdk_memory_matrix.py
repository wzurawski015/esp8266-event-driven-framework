#!/usr/bin/env python3
"""Generate SDK linker-map memory matrix from build logs or ELF section reports."""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUDGETS_DEF = ROOT / "config" / "sdk_memory_budgets.def"
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
REPORT_MD = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"
BUDGET_RE = re.compile(r"^\s*EV_SDK_MEMORY_BUDGET\(\s*([^,]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*,\s*([0-9]+)\s*\)\s*$")
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")

@dataclass(frozen=True)
class Budget:
    target: str
    max_iram: int
    max_dram: int
    max_bss: int
    max_data: int
    max_app_bin: int


def parse_targets() -> dict[str, str]:
    out: dict[str, str] = {}
    for raw in TARGETS_DEF.read_text(encoding="utf-8").splitlines():
        if raw.strip().startswith("EV_SDK_TARGET"):
            m = TARGET_RE.match(raw)
            if not m:
                raise ValueError(f"invalid target row: {raw}")
            out[m.group(1).strip()] = m.group(3).strip()
    return out


def parse_budgets() -> dict[str, Budget]:
    budgets: dict[str, Budget] = {}
    for raw in BUDGETS_DEF.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        m = BUDGET_RE.match(line)
        if not m:
            raise ValueError(f"invalid memory budget row: {raw}")
        name, iram, dram, bss, data, app_bin = m.groups()
        budgets[name] = Budget(name, int(iram), int(dram), int(bss), int(data), int(app_bin))
    return budgets


def parse_ev_mem_log(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    for raw in text.splitlines():
        if raw.startswith("EV_MEM_IRAM"):
            m = re.search(r"used=([0-9]+)", raw)
            if m: values["iram"] = int(m.group(1))
        elif raw.startswith("EV_MEM_DRAM"):
            m = re.search(r"used=([0-9]+)", raw)
            if m: values["dram"] = int(m.group(1))
        elif raw.startswith("EV_MEM_BSS"):
            m = re.search(r"size=([0-9]+)", raw)
            if m: values["bss"] = int(m.group(1))
        elif raw.startswith("EV_MEM_DATA"):
            m = re.search(r"size=([0-9]+)", raw)
            if m: values["data"] = int(m.group(1))
    return values


def status_for(values: dict[str, int], budget: Budget) -> tuple[str, str]:
    if not values:
        return "NOT_RUN", "no EV_MEM markers found"
    checks = [
        ("iram", budget.max_iram),
        ("dram", budget.max_dram),
        ("bss", budget.max_bss),
        ("data", budget.max_data),
    ]
    missing = [name for name, _ in checks if name not in values]
    if missing:
        return "FAIL", "missing memory values: " + ",".join(missing)
    exceeded = [f"{name}={values[name]}>{limit}" for name, limit in checks if values[name] > limit]
    if exceeded:
        return "FAIL", "; ".join(exceeded)
    return "PASS", "within configured section budgets"


def generate_report() -> int:
    targets = parse_targets()
    budgets = parse_budgets()
    errors = []
    for name, klass in targets.items():
        if klass != "metadata_only" and name not in budgets:
            errors.append(f"missing memory budget for {name}")
    for name in budgets:
        if name not in targets:
            errors.append(f"memory budget references unknown target {name}")
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        return 1
    lines = [
        "# SDK linker-map memory matrix report",
        "",
        "PASS requires EV_MEM markers from a real SDK ELF section report. NOT_RUN means no ELF/section report was found.",
        "",
        "| Target | Class | Status | IRAM | DRAM | BSS | DATA | Reason |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for name, klass in targets.items():
        if klass == "metadata_only":
            lines.append(f"| `{name}` | `{klass}` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | metadata-only target |")
            continue
        log_path = ROOT / "logs" / "sdk" / name / "build.log"
        mem_log_path = ROOT / "logs" / "sdk" / name / "memory.log"
        values: dict[str, int] = {}
        if log_path.exists():
            values = parse_ev_mem_log(log_path.read_text(encoding="utf-8", errors="ignore"))
            if values:
                mem_log_path.parent.mkdir(parents=True, exist_ok=True)
                ev_lines = [line for line in log_path.read_text(encoding="utf-8", errors="ignore").splitlines() if line.startswith("EV_MEM_")]
                mem_log_path.write_text("\n".join(ev_lines) + "\n", encoding="utf-8")
        status, reason = status_for(values, budgets[name])
        lines.append(f"| `{name}` | `{klass}` | {status} | {values.get('iram', 0)} | {values.get('dram', 0)} | {values.get('bss', 0)} | {values.get('data', 0)} | {reason} |")
        json_path = ROOT / "logs" / "sdk" / name / "memory.json"
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps({"target": name, "class": klass, "status": status, "values": values, "reason": reason}, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    lines.append("")
    REPORT_MD.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {REPORT_MD}")
    return 0


def self_test() -> int:
    sample = """EV_MEM_IRAM used=1200 limit=32768 free=31568 status=ok\nEV_MEM_DRAM used=4096 limit=81920 free=77824 status=ok\nEV_MEM_BSS size=512 limit=65536 status=ok\nEV_MEM_DATA size=256 limit=32768 status=ok\n"""
    values = parse_ev_mem_log(sample)
    assert values == {"iram": 1200, "dram": 4096, "bss": 512, "data": 256}
    status, _ = status_for(values, Budget("x", 1200, 4096, 512, 256, 1024))
    assert status == "PASS"
    status, _ = status_for(values, Budget("x", 1199, 4096, 512, 256, 1024))
    assert status == "FAIL"
    print("EV_SDK_MEMORY_MATRIX_SELF_TEST PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    return generate_report()

if __name__ == "__main__":
    sys.exit(main())
