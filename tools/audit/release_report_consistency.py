#!/usr/bin/env python3
"""Audit release reports against manifest-backed evidence.

Release reports are read-only summaries.  A PASS in docs/release must point to
committed manifest-backed evidence, never to self-test runs, temp directories or
missing manifests.
"""
from __future__ import annotations

import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
PASS_RE = re.compile(r"\bPASS(?:_[A-Z0-9_]+)?\b")
STATUS_ROW_RE = re.compile(r"\|\s*Status\s*\|\s*([^|]+?)\s*\|")
EVIDENCE_ROW_RE = re.compile(r"\|\s*Evidence dir\s*\|\s*`([^`]+)`\s*\|")
PARSED_ROW_RE = re.compile(r"\|\s*Parsed evidence\s*\|\s*`([^`]+)`\s*\|")
BAD_PASS_PATH_RE = re.compile(r"(^|/)(tmp|var/tmp)|/tmp|TemporaryDirectory|build/selftest|test-run|tmp[a-z0-9_]+", re.I)


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def status_from_report(text: str) -> str:
    m = STATUS_ROW_RE.search(text)
    return m.group(1).strip() if m else "UNKNOWN"


def is_pass_status(status: str) -> bool:
    return status.startswith("PASS") or bool(PASS_RE.fullmatch(status.strip()))


def manifest_for_evidence_dir(text: str) -> Path | None:
    m = EVIDENCE_ROW_RE.search(text)
    if not m:
        return None
    p = Path(m.group(1))
    if not p.is_absolute():
        p = ROOT / p
    return p / "manifest.json"


def parsed_for_eventflow(text: str) -> Path | None:
    m = PARSED_ROW_RE.search(text)
    if not m:
        return None
    p = Path(m.group(1))
    return p if p.is_absolute() else ROOT / p


def load_json(path: Path) -> dict | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return None


def manifest_has_sha(manifest_path: Path) -> bool:
    sha_file = manifest_path.parent / "sha256sums.txt"
    if not sha_file.is_file():
        return False
    text = sha_file.read_text(encoding="utf-8", errors="ignore")
    return "manifest.json" in text


def check_wemos_one_shot_report(report: Path, errors: list[str]) -> None:
    text = report.read_text(encoding="utf-8", errors="ignore")
    status = status_from_report(text)
    pass_status = is_pass_status(status)
    if pass_status:
        if BAD_PASS_PATH_RE.search(text):
            errors.append(f"release-report-consistency: PASS report contains test/tmp path: {rel(report)}")
        manifest_path = manifest_for_evidence_dir(text)
        if manifest_path is None:
            errors.append(f"release-report-consistency: PASS Wemos one-shot report lacks Evidence dir: {rel(report)}")
            return
        # PASS reports must point to committed docs/release evidence, not a temp dir.
        try:
            mrel = manifest_path.relative_to(ROOT).as_posix()
        except ValueError:
            mrel = str(manifest_path)
        if not mrel.startswith("docs/release/wemos_one_shot_evidence/"):
            errors.append(f"release-report-consistency: PASS Wemos one-shot report points outside committed evidence tree: {mrel}")
        data = load_json(manifest_path)
        if data is None:
            errors.append(f"release-report-consistency: PASS Wemos one-shot report missing/invalid manifest: {mrel}")
            return
        mstatus = str(data.get("status", "UNKNOWN"))
        if mstatus != status:
            errors.append(f"release-report-consistency: Wemos report status {status} mismatches manifest status {mstatus}: {rel(report)}")
        if str(data.get("run_id", "")) == "test-run":
            errors.append(f"release-report-consistency: Wemos PASS manifest uses test-run: {mrel}")
        if not manifest_has_sha(manifest_path):
            errors.append(f"release-report-consistency: Wemos PASS manifest lacks sha256sums entry: {mrel}")
    else:
        # Non-PASS reports may be blocked, but should not still carry obvious fake run paths.
        if "Status | ENVIRONMENT_BLOCKED" not in text and "Status | NOT_RUN" not in text and "Status | FAIL" not in text and status == "UNKNOWN":
            errors.append(f"release-report-consistency: Wemos one-shot report lacks explicit status: {rel(report)}")


def check_eventflow_report(report: Path, errors: list[str]) -> None:
    text = report.read_text(encoding="utf-8", errors="ignore")
    status = status_from_report(text)
    if not is_pass_status(status):
        return
    if BAD_PASS_PATH_RE.search(text):
        errors.append(f"release-report-consistency: eventflow PASS report contains test/tmp path: {rel(report)}")
    parsed = parsed_for_eventflow(text)
    if parsed is None:
        errors.append(f"release-report-consistency: eventflow PASS report lacks parsed evidence path: {rel(report)}")
        return
    data = load_json(parsed)
    if data is None:
        errors.append(f"release-report-consistency: eventflow PASS report missing/invalid parsed JSON: {rel(parsed)}")
        return
    if str(data.get("status", "")) != "PASS":
        errors.append(f"release-report-consistency: eventflow report PASS but parsed status is {data.get('status')}: {rel(report)}")
    for source in data.get("sources", []):
        if isinstance(source, dict) and source.get("required") and source.get("status") != "PASS":
            errors.append(f"release-report-consistency: eventflow PASS has non-PASS required source {source.get('name')}")


def collect_errors(root: Path = ROOT) -> list[str]:
    global ROOT
    old_root = ROOT
    ROOT = root
    try:
        errors: list[str] = []
        wemos = root / "docs" / "release" / "wemos_one_shot_evidence_report.md"
        if wemos.is_file():
            check_wemos_one_shot_report(wemos, errors)
        # Future one-shot reports can be added here; scan common eventflow reports too.
        for rel_path in [
            "docs/release/eventflow_hardware_evidence_report.md",
            "docs/release/eventflow_final_hardware_release_report.md",
            "docs/release/eventflow_one_shot_wemos_integration_report.md",
        ]:
            path = root / rel_path
            if path.is_file():
                check_eventflow_report(path, errors)
        return errors
    finally:
        ROOT = old_root


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        docs = root / "docs" / "release"
        good_dir = docs / "wemos_one_shot_evidence" / "wemos_esp_wroom_02_18650" / "current"
        good_dir.mkdir(parents=True)
        (good_dir / "manifest.json").write_text(json.dumps({"status": "PASS_FULL_BUILD_FLASH_SMOKE", "run_id": "real-run"}), encoding="utf-8")
        (good_dir / "sha256sums.txt").write_text("abc  manifest.json\n", encoding="utf-8")
        (docs / "wemos_one_shot_evidence_report.md").write_text("""# Wemos one-shot evidence run\n\n| Field | Value |\n|---|---|\n| Status | PASS_FULL_BUILD_FLASH_SMOKE |\n| Evidence dir | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current` |\n""", encoding="utf-8")
        assert collect_errors(root) == []
        (docs / "wemos_one_shot_evidence_report.md").write_text("""| Field | Value |\n|---|---|\n| Status | PASS_FULL_BUILD_FLASH_SMOKE |\n| Run ID | test-run |\n| Evidence dir | `tmpabc/runs/test-run` |\n""", encoding="utf-8")
        assert collect_errors(root)
        (docs / "wemos_one_shot_evidence_report.md").write_text("""| Field | Value |\n|---|---|\n| Status | ENVIRONMENT_BLOCKED |\n| Reason | manifest not found |\n""", encoding="utf-8")
        assert collect_errors(root) == []
        (good_dir / "manifest.json").write_text(json.dumps({"status": "PASS_SMOKE_ONLY", "run_id": "real-run"}), encoding="utf-8")
        (docs / "wemos_one_shot_evidence_report.md").write_text("""| Field | Value |\n|---|---|\n| Status | PASS_FULL_BUILD_FLASH_SMOKE |\n| Evidence dir | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current` |\n""", encoding="utf-8")
        assert collect_errors(root)


def main() -> int:
    if "--self-test" in sys.argv:
        self_test()
        print("release report consistency self-test passed")
        return 0
    errors = collect_errors(ROOT)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("release report consistency passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
