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


def _sdk_pass_evidence_errors(target: str, evidence: dict, log_text: str | None = None) -> list[str]:
    out: list[str] = []
    values = evidence.get("values", {}) if isinstance(evidence.get("values", {}), dict) else {}
    klass = str(evidence.get("class", ""))
    if evidence.get("status") != "PASS":
        out.append(f"SDK evidence status is not PASS: {target}")
    if evidence.get("evidence_kind") != "sdk_build":
        out.append(f"SDK PASS evidence_kind is not sdk_build: {target}")
    if evidence.get("target") != target:
        out.append(f"SDK PASS evidence target mismatch: {target}")
    if not evidence.get("target_marker_seen") or not evidence.get("target_marker_match"):
        out.append(f"SDK PASS without exact EV_SDK_BUILD_TARGET marker: {target}")
    if not evidence.get("build_status_marker_seen"):
        out.append(f"SDK PASS without EV_SDK_BUILD_STATUS=PASS: {target}")
    if evidence.get("self_test_marker_seen"):
        out.append(f"SDK PASS contains self-test marker: {target}")
    if evidence.get("mixed_transcript_detected"):
        out.append(f"SDK PASS comes from mixed transcript: {target}")
    if klass in {"buildable_sdk", "physical_smoke", "hil_sdk"} and int(values.get("APP_BIN", 0) or 0) <= 0:
        out.append(f"SDK PASS without non-zero APP_BIN: {target}")
    if klass in {"buildable_sdk", "physical_smoke", "hil_sdk"} and not any(int(values.get(key, 0) or 0) > 0 for key in ["IRAM", "DRAM", "BSS", "DATA", "IROM"]):
        out.append(f"SDK PASS without non-zero real memory section: {target}")
    if log_text is not None:
        if f"EV_SDK_BUILD_TARGET={target}" not in log_text:
            out.append(f"SDK PASS build log lacks matching EV_SDK_BUILD_TARGET: {target}")
        if "EV_SDK_BUILD_STATUS=PASS" not in log_text:
            out.append(f"SDK PASS build log lacks EV_SDK_BUILD_STATUS=PASS: {target}")
        if "EV_MEM_REPORT_RESULT PASS target=self-test" in log_text or "--self-test" in log_text or "SELF_TEST PASS" in log_text:
            out.append(f"SDK PASS build log contains self-test transcript: {target}")
    for rel_key in ["build_log", "size_log", "map_summary", "stack_usage", "sdkconfig_effective"]:
        rel = str(evidence.get(rel_key, ""))
        if rel and rel.lower().endswith((".elf", ".bin", ".o", ".a")):
            out.append(f"forbidden binary SDK artifact in evidence: {target}:{rel}")
    return out


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
        log_path = ROOT / str(evidence.get("build_log", ""))
        if not evidence.get("build_log") or not log_path.is_file():
            errors.append(f"release-evidence: SDK PASS row has no committed build log: {target}")
            log_text = None
        else:
            log_text = log_path.read_text(encoding="utf-8", errors="ignore")
        for err in _sdk_pass_evidence_errors(target, evidence, log_text):
            errors.append(f"release-evidence: {err}")

def check_sdk_import_evidence_contracts(errors: list[str]) -> None:
    importer = ROOT / "tools" / "release" / "import_sdk_evidence.py"
    flash_parser = ROOT / "tools" / "release" / "parse_esptool_flash_log.py"
    manifest = ROOT / "config" / "sdk_evidence_import.def"
    report = ROOT / "docs" / "release" / "sdk_imported_build_map_stack_evidence_report.md"
    if not importer.is_file():
        errors.append("release-evidence: SDK import tool missing")
    if not flash_parser.is_file():
        errors.append("release-evidence: esptool flash parser missing")
    if not manifest.is_file():
        errors.append("release-evidence: SDK import manifest missing")
    if not report.is_file():
        errors.append("release-evidence: SDK imported evidence report missing")
    for target_dir in SDK_EVIDENCE_ROOT.glob("*") if SDK_EVIDENCE_ROOT.exists() else []:
        ev = target_dir / "evidence.json"
        if not ev.is_file():
            continue
        data = _load_evidence_for_target(target_dir.name)
        if data.get("status") == "PASS":
            log_path = ROOT / str(data.get("build_log", ""))
            log_text = log_path.read_text(encoding="utf-8", errors="ignore") if log_path.is_file() else None
            for err in _sdk_pass_evidence_errors(target_dir.name, data, log_text):
                errors.append(f"release-evidence: imported {err}")
        flash_ev = target_dir / "flash_evidence.json"
        if flash_ev.is_file():
            try:
                import json
                flash = json.loads(flash_ev.read_text(encoding="utf-8", errors="ignore"))
            except Exception:
                errors.append(f"release-evidence: invalid flash evidence JSON: {target_dir.name}")
                continue
            if flash.get("status") == "PASS":
                if flash.get("evidence_kind") != "esptool_flash":
                    errors.append(f"release-evidence: flash PASS has wrong evidence_kind: {target_dir.name}")
                if flash.get("target") != target_dir.name:
                    errors.append(f"release-evidence: flash PASS target mismatch: {target_dir.name}")
                if flash.get("chip_detected") != "ESP8266EX" or not flash.get("write_seen") or not flash.get("hash_verified"):
                    errors.append(f"release-evidence: flash PASS without ESP8266EX/write/hash proof: {target_dir.name}")
                if not flash.get("flash_log_sha256"):
                    errors.append(f"release-evidence: flash PASS lacks flash log SHA-256: {target_dir.name}")

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
    if not parsed.get("serial_sha256"):
        errors.append("release-evidence: ATNEL I2C PASS without serial SHA-256")


def _parsed_json_path_from_report(path: Path) -> Path | None:
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8", errors="ignore")
    m = re.search(r"docs/release/hil_evidence/[^`| ]+/parsed\.json", text)
    if not m:
        return None
    return ROOT / m.group(0)


def check_wemos_hil_evidence(errors: list[str]) -> None:
    reports = [
        ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_smoke_report.md",
        ROOT / "docs" / "release" / "wemos_esp_wroom_02_18650_deep_sleep_wake_report.md",
    ]
    for path in reports:
        if first_status(path) != "PASS":
            continue
        parsed_path = _parsed_json_path_from_report(path)
        if parsed_path is None or not parsed_path.is_file():
            errors.append(f"release-evidence: {path.relative_to(ROOT).as_posix()} PASS without parsed JSON")
            continue
        try:
            import json
            parsed = json.loads(parsed_path.read_text(encoding="utf-8", errors="ignore"))
        except Exception:
            errors.append(f"release-evidence: {path.relative_to(ROOT).as_posix()} parsed JSON invalid")
            continue
        if parsed.get("status") != "PASS":
            errors.append(f"release-evidence: {path.relative_to(ROOT).as_posix()} PASS but parsed status is not PASS")
        if not parsed.get("serial_sha256"):
            errors.append(f"release-evidence: {path.relative_to(ROOT).as_posix()} PASS without serial SHA-256")
        if "deep_sleep" in path.name and parsed.get("mode") != "deepsleep":
            errors.append("release-evidence: Wemos deep-sleep PASS without deepsleep parsed mode")


def check_eventflow_evidence(errors: list[str]) -> None:
    reports = [
        ROOT / "docs" / "release" / "eventflow_hardware_evidence_report.md",
        ROOT / "docs" / "release" / "eventflow_final_hardware_release_report.md",
    ]
    for report in reports:
        if first_status(report) != "PASS":
            continue
        parsed_path = _parsed_json_path_from_report(report)
        if parsed_path is None or not parsed_path.is_file():
            errors.append(f"release-evidence: {report.relative_to(ROOT).as_posix()} PASS without parsed JSON")
            continue
        try:
            import json
            parsed = json.loads(parsed_path.read_text(encoding="utf-8", errors="ignore"))
        except Exception:
            errors.append(f"release-evidence: {report.relative_to(ROOT).as_posix()} parsed JSON invalid")
            continue
        if parsed.get("status") != "PASS":
            errors.append(f"release-evidence: {report.relative_to(ROOT).as_posix()} PASS but parsed status is not PASS")
        required_names = {"sdk_esp8266_generic_dev", "sdk_atnel_i2c_hil", "sdk_wemos_smoke", "atnel_i2c_sda_stuck_low", "wemos_smoke", "wemos_deep_sleep_wake"}
        seen = set()
        for source in parsed.get("sources", []):
            if source.get("required"):
                seen.add(source.get("name"))
                if source.get("status") != "PASS":
                    errors.append(f"release-evidence: eventflow PASS with non-PASS source {source.get('name')}")
                if not source.get("path_sha256"):
                    errors.append(f"release-evidence: eventflow PASS source lacks SHA-256 {source.get('name')}")
        missing = required_names - seen
        if missing:
            errors.append("release-evidence: eventflow PASS missing required sources: " + ",".join(sorted(missing)))


def check_hil_import_contracts(errors: list[str]) -> None:
    importer = ROOT / "tools" / "hil" / "import_hil_serial_evidence.py"
    if not importer.is_file():
        errors.append("release-evidence: HIL serial import tool missing")
    for rel in [
        "docs/release/hil_serial_evidence_import_workflow.md",
        "docs/release/hil_real_atnel_wemos_evidence_report.md",
    ]:
        if not (ROOT / rel).is_file():
            errors.append(f"release-evidence: HIL import documentation missing: {rel}")

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
    check_eventflow_evidence(errors)
    check_wemos_hil_evidence(errors)
    check_i2c_hil_evidence(errors)
    check_sdk_evidence_files(errors)
    check_sdk_import_evidence_contracts(errors)
    check_hil_import_contracts(errors)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("release evidence contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
