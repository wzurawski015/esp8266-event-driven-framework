#!/usr/bin/env python3
"""Redact committed release-evidence text and repair local evidence hashes.

The script is intentionally value-agnostic: it never prints or embeds private
secret values.  It applies the shared redaction rules to release evidence files
and then refreshes Wemos one-shot manifest/hash sidecars that reference the
redacted serial logs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import tempfile
from pathlib import Path
from typing import Iterable
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_redaction import redact_text
from ev_privacy_classification import PrivacyClass, classify_path

TEXT_SUFFIXES = {".log", ".txt", ".md", ".json", ".jsonl"}
DEFAULT_ROOT = ROOT / "docs" / "release"


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def iter_text_files(root: Path) -> Iterable[Path]:
    for path in root.rglob("*"):
        if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES:
            yield path


def redact_file(path: Path, *, apply: bool, repo_root: Path = ROOT, repo_mode: str = "PRIVATE_REPO") -> bool:
    classified = classify_path(path, root=repo_root, repo_mode=repo_mode)
    if classified.privacy_class == PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE:
        return False
    raw = path.read_text(encoding="utf-8", errors="ignore")
    redacted = redact_text(raw)
    changed = redacted != raw
    if changed and apply:
        path.write_text(redacted, encoding="utf-8")
    return changed


def update_json(path: Path, mutator) -> bool:
    if not path.is_file():
        return False
    try:
        data = json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return False
    before = json.dumps(data, sort_keys=True)
    mutator(data)
    after = json.dumps(data, sort_keys=True)
    if after != before:
        path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        return True
    return False


def _annotate_wemos_privacy_classes(run_dir: Path) -> int:
    manifest = run_dir / "manifest.json"
    if not manifest.is_file():
        return 0
    def update(data: dict) -> None:
        classes = data.setdefault("privacy_classes", {})
        if isinstance(classes, dict):
            if (run_dir / "serial.raw.log").is_file():
                classes["serial.raw.log"] = "PRIVATE_LAB_RAW_EVIDENCE"
            if (run_dir / "serial.log").is_file():
                classes["serial.log"] = "PRIVATE_LAB_RAW_EVIDENCE"
            for name in ["serial.redacted.log", "serial.normalized.log", "parsed.json", "target_timing.json", "excerpt.md", "target_timing_report.md"]:
                if (run_dir / name).is_file():
                    classes[name] = "PUBLIC_SANITIZED"
        for stage in data.get("stages", []) if isinstance(data.get("stages"), list) else []:
            if isinstance(stage, dict) and stage.get("log") in {"serial.raw.log", "serial.log"}:
                stage["privacy_class"] = "PRIVATE_LAB_RAW_EVIDENCE"
                stage["public_export"] = False
    return 1 if update_json(manifest, update) else 0


def repair_wemos_run(run_dir: Path) -> int:
    serial_log = run_dir / "serial.log"
    serial_raw = run_dir / "serial.raw.log"
    serial_normalized = run_dir / "serial.normalized.log"
    serial_redacted = run_dir / "serial.redacted.log"

    changed = _annotate_wemos_privacy_classes(run_dir)
    serial_sha = sha256(serial_log) if serial_log.is_file() else ""
    serial_raw_sha = sha256(serial_raw) if serial_raw.is_file() else serial_sha
    serial_normalized_sha = sha256(serial_normalized) if serial_normalized.is_file() else serial_sha
    serial_redacted_sha = sha256(serial_redacted) if serial_redacted.is_file() else serial_sha

    def update_parser_json(data: dict) -> None:
        if "source_serial_log_sha256" in data and serial_raw_sha:
            data["source_serial_log_sha256"] = serial_raw_sha
        if "redacted_log_sha256" in data and serial_redacted_sha:
            data["redacted_log_sha256"] = serial_redacted_sha

    if update_json(run_dir / "parsed.json", update_parser_json):
        changed += 1
    if update_json(run_dir / "target_timing.json", update_parser_json):
        changed += 1

    def update_manifest(data: dict) -> None:
        for stage in data.get("stages", []) if isinstance(data.get("stages", []), list) else []:
            log = stage.get("log")
            if isinstance(log, str) and (run_dir / log).is_file():
                stage["sha256"] = sha256(run_dir / log)
        smoke = data.get("wemos_smoke") if isinstance(data.get("wemos_smoke"), dict) else None
        if smoke is not None:
            if serial_sha:
                smoke["serial_sha256"] = serial_sha
            if serial_raw_sha:
                smoke["serial_raw_sha256"] = serial_raw_sha
            if serial_normalized_sha:
                smoke["serial_normalized_sha256"] = serial_normalized_sha
        timing = data.get("target_timing") if isinstance(data.get("target_timing"), dict) else None
        if timing is not None and (run_dir / "target_timing.json").is_file():
            timing["sha256"] = sha256(run_dir / "target_timing.json")

    if update_json(run_dir / "manifest.json", update_manifest):
        changed += 1

    sums = run_dir / "sha256sums.txt"
    if sums.is_file():
        refreshed: list[str] = []
        for raw in sums.read_text(encoding="utf-8", errors="ignore").splitlines():
            parts = raw.split(None, 1)
            if len(parts) != 2:
                continue
            rel = parts[1]
            target = run_dir / rel
            if target.is_file():
                refreshed.append(f"{sha256(target)}  {rel}")
        new_text = "\n".join(refreshed) + ("\n" if refreshed else "")
        if new_text and new_text != sums.read_text(encoding="utf-8", errors="ignore"):
            sums.write_text(new_text, encoding="utf-8")
            changed += 1
    return changed


def repair_wemos_runs(root: Path, *, apply: bool) -> int:
    if not apply:
        return 0
    changed = 0
    for manifest in root.glob("**/wemos_one_shot_evidence/**/runs/*/manifest.json"):
        changed += repair_wemos_run(manifest.parent)
    return changed


def redact_tree(root: Path, *, apply: bool, repo_mode: str = "PRIVATE_REPO") -> tuple[int, int]:
    repo_root = root.resolve().parents[1] if len(root.resolve().parents) >= 2 else ROOT
    files_changed = 0
    for path in iter_text_files(root):
        if redact_file(path, apply=apply, repo_root=repo_root, repo_mode=repo_mode):
            files_changed += 1
    repairs = repair_wemos_runs(root, apply=apply)
    return files_changed, repairs


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "docs" / "release"
        run = root / "wemos_one_shot_evidence" / "wemos_esp_wroom_02_18650" / "current" / "runs" / "sample"
        run.mkdir(parents=True)
        serial = "I (1) wifi:connected with lab-ssid-value, aid = 1\n"
        for name in ["serial.log", "serial.raw.log", "serial.normalized.log", "serial.redacted.log"]:
            (run / name).write_text(serial, encoding="utf-8")
        old_sha = sha256(run / "serial.raw.log")
        (run / "parsed.json").write_text(json.dumps({"source_serial_log_sha256": old_sha, "redacted_log_sha256": old_sha}) + "\n", encoding="utf-8")
        (run / "target_timing.json").write_text(json.dumps({"source_serial_log_sha256": old_sha, "redacted_log_sha256": old_sha}) + "\n", encoding="utf-8")
        manifest = {"evidence_kind": "wemos_one_shot_bundle", "operator_intent": {"ok": True, "operator_acknowledged_private_repo_secrets": True}, "stages": [{"log": "serial.raw.log", "sha256": old_sha}, {"log": "parsed.json", "sha256": sha256(run / "parsed.json")}], "wemos_smoke": {"serial_sha256": old_sha, "serial_raw_sha256": old_sha, "serial_normalized_sha256": old_sha}, "target_timing": {"sha256": sha256(run / "target_timing.json")}}
        (run / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        (run / "sha256sums.txt").write_text(f"{old_sha}  serial.raw.log\n{sha256(run / 'manifest.json')}  manifest.json\n", encoding="utf-8")
        files, repairs = redact_tree(root, apply=True)
        assert files >= 2
        assert repairs >= 1
        assert "lab-ssid-value" in (run / "serial.raw.log").read_text(encoding="utf-8", errors="ignore")
        assert "lab-ssid-value" in (run / "serial.log").read_text(encoding="utf-8", errors="ignore")
        assert "lab-ssid-value" not in (run / "serial.redacted.log").read_text(encoding="utf-8", errors="ignore")
        assert "lab-ssid-value" not in (run / "serial.normalized.log").read_text(encoding="utf-8", errors="ignore")
        repaired = json.loads((run / "manifest.json").read_text(encoding="utf-8"))
        assert repaired["stages"][0]["sha256"] == sha256(run / "serial.raw.log")
    print("EV_EXISTING_EVIDENCE_REDACTION_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--mode", choices=["PRIVATE_REPO", "PUBLIC_RELEASE"], default="PRIVATE_REPO")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    files, repairs = redact_tree(args.root, apply=args.apply, repo_mode=args.mode)
    mode = "APPLY" if args.apply else "DRY_RUN"
    print(f"EV_EXISTING_EVIDENCE_REDACTION {mode} files_changed={files} hash_repairs={repairs}")
    return 0 if args.apply or files == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
