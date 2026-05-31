#!/usr/bin/env python3
"""Validate that release PASS rows are backed by committed evidence."""
from __future__ import annotations

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
STATUS_VALUES = {"PASS", "FAIL", "NOT_RUN", "ENVIRONMENT_BLOCKED", "NOT_APPLICABLE"}
SDK_BUILD_REPORT = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
SDK_MEMORY_REPORT = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"

SDK_EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
SDK_REAL_EVIDENCE_REPORT = ROOT / "docs" / "release" / "sdk_real_build_map_stack_evidence_report.md"


def _evidence_json_for_target(target: str) -> Path:
    return SDK_EVIDENCE_ROOT / target / "evidence.json"


def _load_evidence_for_target(target: str) -> dict:
    path = _evidence_json_for_target(target)
    if not path.is_file():
        return {}
    try:
        import json
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return {}


def check_sdk_evidence_files(errors: list[str]) -> None:
    rows = table_rows(SDK_BUILD_REPORT)
    header = next((cells for cells in rows if cells and cells[0] == "Target"), [])
    if not header:
        return
    try:
        status_index = header.index("Status")
    except ValueError:
        return
    for cells in rows:
        if not cells or cells[0] == "Target" or len(cells) <= status_index:
            continue
        target = cells[0].strip("`")
        status = cells[status_index]
        if status != "PASS":
            continue
        evidence = _load_evidence_for_target(target)
        if not evidence:
            errors.append(f"release-evidence: SDK PASS row has no evidence.json: {target}")
            continue
        log_path = evidence.get("build_log", "")
        if not log_path or not (ROOT / str(log_path)).is_file():
            errors.append(f"release-evidence: SDK PASS row has no committed build log: {target}")
        if evidence.get("status") != "PASS":
            errors.append(f"release-evidence: SDK PASS row evidence status is not PASS: {target}")
        values = evidence.get("values", {}) if isinstance(evidence.get("values", {}), dict) else {}
        if not any(int(values.get(key, 0) or 0) > 0 for key in ["IRAM", "DRAM", "BSS", "DATA", "APP_BIN"]):
            errors.append(f"release-evidence: SDK PASS row has no non-zero memory evidence: {target}")
FINAL_SUMMARY = ROOT / "docs" / "release" / "final_release_validation_summary.md"
HIL_REPORTS = [
    ROOT / "docs" / "release" / "hil_atnel_i2c_report.md",
    ROOT / "docs" / "release" / "hil_atnel_onewire_report.md",
    ROOT / "docs" / "release" / "hil_atnel_wifi_report.md",
    ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md",
]


def table_rows(path: Path) -> list[list[str]]:
    if not path.exists():
        return []
    rows: list[list[str]] = []
    for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line.startswith("|") or line.startswith("|---"):
            continue
        cells = [cell.strip().strip("`") for cell in line.strip("|").split("|")]
        if any(cell for cell in cells):
            rows.append(cells)
    return rows


def first_status(path: Path) -> str:
    for cells in table_rows(path):
        if len(cells) >= 2 and cells[0] == "Status" and cells[1] in STATUS_VALUES:
            return cells[1]
    return "NOT_RUN"


def status_cells(cells: list[str]) -> list[str]:
    return [cell for cell in cells if cell in STATUS_VALUES]


def check_sdk_build_report(errors: list[str]) -> dict[str, str]:
    group_status: dict[str, list[str]] = {"buildable_sdk": [], "physical_smoke": [], "hil_sdk": []}
    rows = table_rows(SDK_BUILD_REPORT)
    header = next((cells for cells in rows if cells and cells[0] == "Target"), [])
    try:
        class_index = header.index("Class")
        status_index = header.index("Status")
    except ValueError:
        errors.append("release-evidence: SDK build matrix report is missing Class/Status columns")
        return {klass: "NOT_RUN" for klass in group_status}
    log_index = header.index("Log") if "Log" in header else None

    for cells in rows:
        if not cells or cells[0] == "Target":
            continue
        if len(cells) <= max(class_index, status_index):
            continue
        target = cells[0]
        klass = cells[class_index]
        status = cells[status_index]
        log_path = cells[log_index] if log_index is not None and len(cells) > log_index else ""
        if klass in group_status:
            group_status[klass].append(status)
        if status == "PASS":
            if not log_path:
                errors.append(f"release-evidence: SDK PASS row has no log path: {target}")
            elif not (ROOT / log_path).is_file():
                errors.append(f"release-evidence: SDK PASS row references missing log: {target} {log_path}")
    result: dict[str, str] = {}
    for klass, statuses in group_status.items():
        if not statuses:
            result[klass] = "NOT_RUN"
        elif any(status == "FAIL" for status in statuses):
            result[klass] = "FAIL"
        elif any(status == "ENVIRONMENT_BLOCKED" for status in statuses):
            result[klass] = "ENVIRONMENT_BLOCKED"
        elif any(status == "NOT_RUN" for status in statuses):
            result[klass] = "NOT_RUN"
        elif all(status in {"PASS", "NOT_APPLICABLE"} for status in statuses):
            result[klass] = "PASS"
        else:
            result[klass] = "NOT_RUN"
    return result


def check_sdk_memory_report(errors: list[str]) -> dict[str, str]:
    group_status: dict[str, list[str]] = {"buildable_sdk": [], "physical_smoke": [], "hil_sdk": []}
    rows = table_rows(SDK_MEMORY_REPORT)
    header = next((cells for cells in rows if cells and cells[0] == "Target"), [])
    try:
        class_index = header.index("Class")
        status_index = header.index("Status")
    except ValueError:
        errors.append("release-evidence: SDK memory matrix report is missing Class/Status columns")
        return {klass: "NOT_RUN" for klass in group_status}
    evidence_indexes = [header.index(name) for name in ["IRAM", "DRAM", "BSS", "DATA", "APP_BIN"] if name in header]

    for cells in rows:
        if not cells or cells[0] == "Target":
            continue
        if len(cells) <= max(class_index, status_index):
            continue
        target = cells[0]
        klass = cells[class_index]
        status = cells[status_index]
        if klass in group_status:
            group_status[klass].append(status)
        if status == "PASS":
            parsed = []
            for index in evidence_indexes:
                try:
                    parsed.append(int(cells[index]))
                except (IndexError, ValueError):
                    parsed.append(0)
            if not any(value > 0 for value in parsed):
                errors.append(f"release-evidence: SDK memory PASS row has no non-zero EV_MEM evidence: {target}")
    result: dict[str, str] = {}
    for klass, statuses in group_status.items():
        if not statuses:
            result[klass] = "NOT_RUN"
        elif any(status == "FAIL" for status in statuses):
            result[klass] = "FAIL"
        elif any(status == "ENVIRONMENT_BLOCKED" for status in statuses):
            result[klass] = "ENVIRONMENT_BLOCKED"
        elif any(status == "NOT_RUN" for status in statuses):
            result[klass] = "NOT_RUN"
        elif all(status in {"PASS", "NOT_APPLICABLE"} for status in statuses):
            result[klass] = "PASS"
        else:
            result[klass] = "NOT_RUN"
    return result


def summary_rows() -> dict[str, str]:
    rows: dict[str, str] = {}
    for cells in table_rows(FINAL_SUMMARY):
        if len(cells) >= 2 and cells[0] != "Area" and cells[1] in STATUS_VALUES:
            rows[cells[0]] = cells[1]
    return rows


def check_final_summary(errors: list[str], sdk_status: dict[str, str], mem_status: dict[str, str]) -> None:
    rows = summary_rows()
    buildable = "PASS" if sdk_status.get("buildable_sdk") == "PASS" and sdk_status.get("physical_smoke") == "PASS" else "NOT_RUN"
    if rows.get("SDK build matrix: buildable targets") == "PASS" and buildable != "PASS":
        errors.append("release-evidence: final summary reports SDK buildable PASS without SDK build evidence")
    if rows.get("SDK build matrix: HIL SDK targets") == "PASS" and sdk_status.get("hil_sdk") != "PASS":
        errors.append("release-evidence: final summary reports HIL SDK PASS without SDK build evidence")
    mem_buildable = "PASS" if mem_status.get("buildable_sdk") == "PASS" and mem_status.get("physical_smoke") == "PASS" else "NOT_RUN"
    if rows.get("SDK linker-map memory matrix: buildable targets") == "PASS" and mem_buildable != "PASS":
        errors.append("release-evidence: final summary reports SDK memory PASS without EV_MEM evidence")
    if rows.get("SDK linker-map memory matrix: HIL SDK targets") == "PASS" and mem_status.get("hil_sdk") != "PASS":
        errors.append("release-evidence: final summary reports HIL SDK memory PASS without EV_MEM evidence")


def check_hil_reports(errors: list[str]) -> None:
    for path in HIL_REPORTS:
        status = first_status(path)
        if status != "PASS":
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "EV_HIL_RESULT PASS" in text or "EV_WEMOS_SMOKE_RUNTIME_READY" in text:
            continue
        errors.append(f"release-evidence: {path.relative_to(ROOT).as_posix()} reports PASS without serial marker evidence")



def check_i2c_hil_evidence(errors: list[str]) -> None:
    path = ROOT / "docs" / "release" / "hil_atnel_i2c_report.md"
    if first_status(path) != "PASS":
        return
    text = path.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"docs/release/hil_evidence/i2c/[^`| ]+/parsed\.json", text)
    if not m:
        errors.append("release-evidence: ATNEL I2C PASS without parsed JSON path")
        return
    parsed_path = ROOT / m.group(0)
    if not parsed_path.is_file():
        errors.append("release-evidence: ATNEL I2C PASS parsed JSON is missing")
        return
    try:
        import json
        parsed = json.loads(parsed_path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        errors.append("release-evidence: ATNEL I2C parsed JSON is invalid")
        return
    if parsed.get("status") != "PASS" or parsed.get("case") != "sda-stuck-low-containment":
        errors.append("release-evidence: ATNEL I2C PASS without sda-stuck-low parsed PASS")
    if not parsed.get("fixture_coupled", False):
        errors.append("release-evidence: ATNEL I2C PASS without fixture-coupled evidence")

def self_test() -> None:
    assert status_cells(["foo", "PASS", "bar"]) == ["PASS"]
    assert status_cells(["foo", "NOT_RUN"]) == ["NOT_RUN"]


def main() -> int:
    self_test()
    errors: list[str] = []
    sdk_status = check_sdk_build_report(errors)
    mem_status = check_sdk_memory_report(errors)
    check_final_summary(errors, sdk_status, mem_status)
    check_hil_reports(errors)
    check_i2c_hil_evidence(errors)
    check_sdk_evidence_files(errors)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("release evidence contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
