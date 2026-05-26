#!/usr/bin/env python3
"""Generate SDK linker-map memory matrix from build logs or ELF section reports."""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
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


@dataclass
class MemorySample:
    values: dict[str, int] = field(default_factory=dict)
    app_bin_status: str | None = None
    stack_status: str | None = None
    stack_files: int = 0
    stack_entries: int = 0
    stack_max_frame: int = 0
    stack_function: str = "unknown"


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


def parse_key_value_int(raw: str, key: str) -> int | None:
    m = re.search(rf"(?:^|\s){re.escape(key)}=([0-9]+)(?:\s|$)", raw)
    return int(m.group(1)) if m else None


def parse_key_value_text(raw: str, key: str) -> str | None:
    m = re.search(rf"(?:^|\s){re.escape(key)}=([^\s]+)(?:\s|$)", raw)
    return m.group(1) if m else None


def parse_ev_mem_log(text: str) -> MemorySample:
    sample = MemorySample()
    for raw in text.splitlines():
        if raw.startswith("EV_MEM_IRAM"):
            used = parse_key_value_int(raw, "used")
            if used is not None:
                sample.values["iram"] = used
        elif raw.startswith("EV_MEM_DRAM"):
            used = parse_key_value_int(raw, "used")
            if used is not None:
                sample.values["dram"] = used
        elif raw.startswith("EV_MEM_BSS"):
            size = parse_key_value_int(raw, "size")
            if size is not None:
                sample.values["bss"] = size
        elif raw.startswith("EV_MEM_DATA"):
            size = parse_key_value_int(raw, "size")
            if size is not None:
                sample.values["data"] = size
        elif raw.startswith("EV_MEM_APP_BIN"):
            status = parse_key_value_text(raw, "status")
            size = parse_key_value_int(raw, "size")
            sample.app_bin_status = status
            if size is not None:
                sample.values["app_bin"] = size
        elif raw.startswith("EV_MEM_STACK_USAGE"):
            sample.stack_status = parse_key_value_text(raw, "status")
            sample.stack_files = parse_key_value_int(raw, "files") or 0
            sample.stack_entries = parse_key_value_int(raw, "entries") or 0
            sample.stack_max_frame = parse_key_value_int(raw, "max_frame") or 0
            sample.stack_function = parse_key_value_text(raw, "function") or "unknown"
    return sample


def status_for(sample: MemorySample, budget: Budget) -> tuple[str, str]:
    if not sample.values:
        return "NOT_RUN", "no EV_MEM markers found"
    checks = [
        ("iram", budget.max_iram),
        ("dram", budget.max_dram),
        ("bss", budget.max_bss),
        ("data", budget.max_data),
        ("app_bin", budget.max_app_bin),
    ]
    missing = [name for name, _ in checks if name not in sample.values]
    if sample.app_bin_status == "not_available":
        missing.append("app_bin")
    if missing:
        return "FAIL", "missing memory values: " + ",".join(dict.fromkeys(missing))
    exceeded = [f"{name}={sample.values[name]}>{limit}" for name, limit in checks if sample.values[name] > limit]
    if exceeded:
        return "FAIL", "; ".join(exceeded)
    return "PASS", "within configured section and app-bin budgets"


def strict_required() -> bool:
    return os.environ.get("EV_SDK_MEMORY_REQUIRE_PASS", "").strip() in {"1", "true", "TRUE", "yes", "YES"}


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

    require_pass = strict_required()
    failing_targets: list[str] = []
    lines = [
        "# SDK linker-map memory matrix report",
        "",
        "PASS requires EV_MEM markers from a real SDK ELF section report. NOT_RUN means no ELF/section report was found.",
        "APP_BIN is checked against `max_app_bin_bytes` from `config/sdk_memory_budgets.def` when a real report provides the application binary size.",
        "STACK is a report-only per-function `.su` max-frame baseline; it is not a call-chain worst-case proof.",
        "Strict release mode is enabled with `EV_SDK_MEMORY_REQUIRE_PASS=1` and fails on non-metadata rows that are not PASS.",
        "",
        "| Target | Class | Status | IRAM | DRAM | BSS | DATA | APP_BIN | STACK | Reason |",
        "|---|---|---:|---:|---:|---:|---:|---:|---|---|",
    ]
    for name, klass in targets.items():
        if klass == "metadata_only":
            lines.append(f"| `{name}` | `{klass}` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | 0 | N/A | metadata-only target |")
            continue
        log_path = ROOT / "logs" / "sdk" / name / "build.log"
        mem_log_path = ROOT / "logs" / "sdk" / name / "memory.log"
        sample = MemorySample()
        if log_path.exists():
            log_text = log_path.read_text(encoding="utf-8", errors="ignore")
            sample = parse_ev_mem_log(log_text)
            if sample.values or sample.stack_status:
                mem_log_path.parent.mkdir(parents=True, exist_ok=True)
                ev_lines = [line for line in log_text.splitlines() if line.startswith("EV_MEM_")]
                mem_log_path.write_text("\n".join(ev_lines) + "\n", encoding="utf-8")
        status, reason = status_for(sample, budgets[name])
        stack_cell = "N/A"
        if sample.stack_status:
            stack_cell = f"{sample.stack_status}:{sample.stack_max_frame}"
        lines.append(
            f"| `{name}` | `{klass}` | {status} | {sample.values.get('iram', 0)} | "
            f"{sample.values.get('dram', 0)} | {sample.values.get('bss', 0)} | "
            f"{sample.values.get('data', 0)} | {sample.values.get('app_bin', 0)} | "
            f"{stack_cell} | {reason} |"
        )
        json_path = ROOT / "logs" / "sdk" / name / "memory.json"
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(
            json.dumps(
                {
                    "target": name,
                    "class": klass,
                    "status": status,
                    "values": sample.values,
                    "app_bin_status": sample.app_bin_status,
                    "stack": {
                        "status": sample.stack_status,
                        "files": sample.stack_files,
                        "entries": sample.stack_entries,
                        "max_frame": sample.stack_max_frame,
                        "function": sample.stack_function,
                    },
                    "reason": reason,
                },
                sort_keys=True,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        if require_pass and status != "PASS":
            failing_targets.append(f"{name}:{status}")
    lines.append("")
    if require_pass:
        lines.append(f"Strict mode: {'PASS' if not failing_targets else 'FAIL'}")
    else:
        lines.append("Report-only mode: non-PASS rows are recorded but do not fail this target.")
    lines.append("")
    REPORT_MD.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {REPORT_MD}")
    if failing_targets:
        print("error: EV_SDK_MEMORY_REQUIRE_PASS=1 failed for " + ", ".join(failing_targets), file=sys.stderr)
        return 1
    return 0


def self_test() -> int:
    sample_text = (
        "EV_MEM_IRAM used=1200 limit=32768 free=31568 status=ok\n"
        "EV_MEM_DRAM used=4096 limit=81920 free=77824 status=ok\n"
        "EV_MEM_BSS size=512 limit=65536 status=ok\n"
        "EV_MEM_DATA size=256 limit=32768 status=ok\n"
        "EV_MEM_APP_BIN size=900 limit=unchecked status=unchecked source=build/app.bin\n"
        "EV_MEM_STACK_USAGE status=unchecked files=2 entries=7 max_frame=128 limit=unchecked function=handler qualifier=static source=main.su\n"
    )
    sample = parse_ev_mem_log(sample_text)
    assert sample.values == {"iram": 1200, "dram": 4096, "bss": 512, "data": 256, "app_bin": 900}
    assert sample.app_bin_status == "unchecked"
    assert sample.stack_status == "unchecked"
    assert sample.stack_files == 2
    assert sample.stack_entries == 7
    assert sample.stack_max_frame == 128
    assert sample.stack_function == "handler"
    status, _ = status_for(sample, Budget("x", 1200, 4096, 512, 256, 900))
    assert status == "PASS"
    status, _ = status_for(sample, Budget("x", 1200, 4096, 512, 256, 899))
    assert status == "FAIL"
    missing_app = parse_ev_mem_log(
        "EV_MEM_IRAM used=1200 limit=32768 free=31568 status=ok\n"
        "EV_MEM_DRAM used=4096 limit=81920 free=77824 status=ok\n"
        "EV_MEM_BSS size=512 limit=65536 status=ok\n"
        "EV_MEM_DATA size=256 limit=32768 status=ok\n"
        "EV_MEM_APP_BIN size=0 limit=unchecked status=not_available source=not_found\n"
    )
    status, reason = status_for(missing_app, Budget("x", 1200, 4096, 512, 256, 1024))
    assert status == "FAIL"
    assert "app_bin" in reason
    assert status_for(MemorySample(), Budget("x", 1, 1, 1, 1, 1))[0] == "NOT_RUN"
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
