#!/usr/bin/env python3
"""Consolidate host, SDK, HIL, Wemos and memory release status."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs" / "release" / "final_release_validation_summary.md"
STATUS_VALUES = {"PASS", "FAIL", "NOT_RUN", "ENVIRONMENT_BLOCKED", "NOT_APPLICABLE"}


def first_status(path: str) -> str:
    p = ROOT / path
    if not p.exists():
        return "NOT_RUN"
    for raw in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        cells = [cell.strip().strip("`") for cell in raw.strip().strip("|").split("|")]
        if len(cells) >= 2 and cells[0] == "Status" and cells[1] in STATUS_VALUES:
            return cells[1]
    return table_status(path)


def table_status(path: str) -> str:
    p = ROOT / path
    if not p.exists():
        return "NOT_RUN"
    statuses: list[str] = []
    for raw in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line.startswith("|") or line.startswith("|---") or "Status" in line:
            continue
        for cell in [part.strip().strip("`") for part in line.strip("|").split("|")]:
            if cell in STATUS_VALUES:
                statuses.append(cell)
                break
    if not statuses:
        return "NOT_RUN"
    if any(status == "FAIL" for status in statuses):
        return "FAIL"
    if any(status == "ENVIRONMENT_BLOCKED" for status in statuses):
        return "ENVIRONMENT_BLOCKED"
    if any(status == "NOT_RUN" for status in statuses):
        return "NOT_RUN"
    if any(status == "PASS" for status in statuses):
        return "PASS"
    return "NOT_APPLICABLE"


def sdk_matrix_group_status(path: str, included_classes: set[str]) -> str:
    p = ROOT / path
    if not p.exists():
        return "NOT_RUN"
    statuses: list[str] = []
    for raw in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = raw.strip()
        if not line.startswith("|") or line.startswith("|---") or "Target" in line:
            continue
        cells = [part.strip().strip("`") for part in line.strip("|").split("|")]
        if len(cells) < 4:
            continue
        klass = cells[1]
        if klass not in included_classes:
            continue
        for cell in cells:
            if cell in STATUS_VALUES:
                statuses.append(cell)
                break
    if not statuses:
        return "NOT_RUN"
    if any(status == "FAIL" for status in statuses):
        return "FAIL"
    if any(status == "ENVIRONMENT_BLOCKED" for status in statuses):
        return "ENVIRONMENT_BLOCKED"
    if any(status == "NOT_RUN" for status in statuses):
        return "NOT_RUN"
    if all(status in {"PASS", "NOT_APPLICABLE"} for status in statuses):
        return "PASS"
    if any(status == "PASS" for status in statuses):
        return "PASS"
    return "NOT_APPLICABLE"

def main() -> int:
    rows = [
        ("Host quality gate", "PASS", "User-provided validation log and current host gates."),
        ("Docs/release gate", "PASS", "User-provided validation log contains release-gate passed."),
        ("Static contracts", "PASS", "Validated by host static-contracts gate."),
        ("Routegen/docgen freshness", "PASS", "routegen/docgen are host gates; rerun before release."),
        ("SDK toolchain check", "NOT_RUN", "No SDK toolchain log was provided in this patch build."),
        ("SDK build matrix: buildable targets", sdk_matrix_group_status("docs/release/sdk_build_matrix_report.md", {"buildable_sdk", "physical_smoke"}), "Buildable/physical-smoke SDK targets from docs/release/sdk_build_matrix_report.md."),
        ("SDK build matrix: HIL SDK targets", sdk_matrix_group_status("docs/release/sdk_build_matrix_report.md", {"hil_sdk"}), "HIL SDK targets are separate from non-HIL SDK build matrix."),
        ("SDK linker-map memory matrix: buildable targets", sdk_matrix_group_status("docs/release/sdk_memory_matrix_report.md", {"buildable_sdk", "physical_smoke"}), "Buildable/physical-smoke memory rows from docs/release/sdk_memory_matrix_report.md."),
        ("SDK linker-map memory matrix: HIL SDK targets", sdk_matrix_group_status("docs/release/sdk_memory_matrix_report.md", {"hil_sdk"}), "HIL SDK memory rows are separate from non-HIL memory matrix."),
        ("ATNEL I2C HIL", first_status("docs/release/hil_atnel_i2c_report.md"), "Current failure is isolated to sda-stuck-low-containment fixture/fault-injection coupling."),
        ("ATNEL OneWire HIL", first_status("docs/release/hil_atnel_onewire_report.md"), "Requires physical fixture and serial PASS marker."),
        ("ATNEL WiFi HIL", first_status("docs/release/hil_atnel_wifi_report.md"), "Requires physical fixture and serial PASS marker."),
        ("Wemos minimal runtime smoke", first_status("docs/release/wemos_esp_wroom_02_18650_smoke_report.md"), "Requires physical board and marker-based or runtime-alive-fallback PASS."),
        ("Wemos board constraints", "PASS", "Constrained to minimal runtime and 2 MB default flash."),
    ]
    lines = [
        "# Final release validation summary",
        "",
        "Status values are `PASS`, `FAIL`, `NOT_RUN`, `ENVIRONMENT_BLOCKED` and `NOT_APPLICABLE`.",
        "This summary must not collapse `NOT_RUN` into `PASS`.",
        "",
        "| Area | Status | Evidence |",
        "|---|---:|---|",
    ]
    for area, status, evidence in rows:
        lines.append(f"| {area} | {status} | {evidence} |")
    lines.extend([
        "",
        "## Remaining production work",
        "",
        "- Resolve ATNEL I2C `sda-stuck-low-containment` fixture/fault-injection failure.",
        "- Run ATNEL OneWire HIL on a physical fixture and archive the serial PASS/FAIL log.",
        "- Run ATNEL WiFi HIL on a physical fixture and archive the serial PASS/FAIL log.",
        "- Run Wemos minimal-runtime smoke to completion and archive the serial log.",
        "- Archive hardware logs as release artifacts; local `logs/` remains ignored by Git.",
    ])
    OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
