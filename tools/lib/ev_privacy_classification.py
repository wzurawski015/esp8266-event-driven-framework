#!/usr/bin/env python3
"""Classify repository artifacts for private-lab secret containment.

The project intentionally supports PRIVATE_REPO mode where board-local secrets
and explicitly raw/private hardware evidence may be tracked.  The classifier is
centralized so audit, release and scrub tools share the same semantics instead
of growing divergent path-prefix exceptions.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PrivacyClass(str, Enum):
    PUBLIC_SANITIZED = "PUBLIC_SANITIZED"
    PRIVATE_LAB_SECRET_SOURCE = "PRIVATE_LAB_SECRET_SOURCE"
    PRIVATE_LAB_RAW_EVIDENCE = "PRIVATE_LAB_RAW_EVIDENCE"
    INVALID_MISLABELED_REDACTED = "INVALID_MISLABELED_REDACTED"
    UNKNOWN = "UNKNOWN"


ALLOWED_LOCAL_SECRET_PATHS = {
    "bsp/wemos_esp_wroom_02_18650/board_secrets.local.h",
    "bsp/atb_thermo_wemos_esp_wroom_02_4mb/board_secrets.local.h",
}

RAW_EVIDENCE_NAMES = {
    "serial.raw.log",
    "operator.raw.log",
    "serial.private_raw.log",
    "operator.private_raw.log",
}

# Legacy Wemos one-shot captures used serial.log as the operator-visible raw log
# beside serial.raw.log.  This is accepted only for that explicit evidence tree.
LEGACY_RAW_EVIDENCE_NAMES = {"serial.log"}

PUBLIC_EVIDENCE_TOKENS = (
    "redacted",
    "normalized",
    "summary",
    "excerpt",
    "report",
)

PUBLIC_EVIDENCE_SUFFIXES = (
    ".md",
    ".json",
    ".jsonl",
)

MANIFEST_NAMES = {"manifest.json", "sha256sums.txt"}


@dataclass(frozen=True)
class Classification:
    rel: str
    privacy_class: PrivacyClass
    reason: str
    public_export: bool


def relpath(path: Path, root: Path = ROOT) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def _load_manifest_for(path: Path) -> dict:
    for parent in [path.parent, *path.parents]:
        manifest = parent / "manifest.json"
        if manifest.is_file():
            try:
                data = json.loads(manifest.read_text(encoding="utf-8", errors="ignore"))
            except Exception:
                return {}
            return data if isinstance(data, dict) else {}
    return {}


def _manifest_declares_private_raw(path: Path, manifest: dict) -> bool:
    name = path.name
    entries = manifest.get("privacy_classes")
    if isinstance(entries, dict):
        value = entries.get(name) or entries.get(path.as_posix())
        if value == PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE.value:
            return True
    for stage in manifest.get("stages", []) if isinstance(manifest.get("stages"), list) else []:
        if not isinstance(stage, dict):
            continue
        if stage.get("log") == name and stage.get("privacy_class") == PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE.value:
            return True
    return False


def _operator_acknowledged_private_repo(manifest: dict) -> bool:
    intent = manifest.get("operator_intent")
    return isinstance(intent, dict) and bool(intent.get("operator_acknowledged_private_repo_secrets"))


def is_release_evidence_path(rel: str) -> bool:
    return rel.startswith("docs/release/")


def classify_path(path: Path, *, root: Path = ROOT, repo_mode: str = "PRIVATE_REPO") -> Classification:
    rel = relpath(path, root)
    name = path.name
    lower_name = name.lower()
    lower_rel = rel.lower()

    if rel in ALLOWED_LOCAL_SECRET_PATHS:
        return Classification(rel, PrivacyClass.PRIVATE_LAB_SECRET_SOURCE, "explicit BSP-local private-lab secret source", False)

    if is_release_evidence_path(rel):
        if name in MANIFEST_NAMES:
            return Classification(rel, PrivacyClass.PUBLIC_SANITIZED, "release manifest/hash sidecar must be sanitized", True)
        if any(token in lower_name for token in PUBLIC_EVIDENCE_TOKENS) or lower_name.endswith(PUBLIC_EVIDENCE_SUFFIXES):
            return Classification(rel, PrivacyClass.PUBLIC_SANITIZED, "public/redacted release evidence artifact", True)
        if name in RAW_EVIDENCE_NAMES:
            return Classification(rel, PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE, "explicit raw/private evidence filename", False)
        if name in LEGACY_RAW_EVIDENCE_NAMES and "wemos_one_shot_evidence" in lower_rel:
            manifest = _load_manifest_for(path)
            if _operator_acknowledged_private_repo(manifest) or _manifest_declares_private_raw(path, manifest):
                return Classification(rel, PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE, "legacy Wemos one-shot raw serial evidence", False)
        if lower_name.endswith((".log", ".txt")):
            return Classification(rel, PrivacyClass.PUBLIC_SANITIZED, "release text evidence without raw/private class", True)

    return Classification(rel, PrivacyClass.UNKNOWN, "ordinary repository file", True)


def may_contain_literal_secret(path: Path, *, root: Path = ROOT, repo_mode: str = "PRIVATE_REPO") -> bool:
    classified = classify_path(path, root=root, repo_mode=repo_mode)
    if repo_mode == "PUBLIC_RELEASE":
        return False
    return classified.privacy_class in {
        PrivacyClass.PRIVATE_LAB_SECRET_SOURCE,
        PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE,
    }


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        secret = root / "bsp" / "wemos_esp_wroom_02_18650" / "board_secrets.local.h"
        secret.parent.mkdir(parents=True)
        secret.write_text("#define EV_BOARD_NET_WIFI_SSID \"lab\"\n", encoding="utf-8")
        assert classify_path(secret, root=root).privacy_class == PrivacyClass.PRIVATE_LAB_SECRET_SOURCE

        run = root / "docs" / "release" / "wemos_one_shot_evidence" / "wemos" / "current" / "runs" / "r1"
        run.mkdir(parents=True)
        (run / "manifest.json").write_text(
            json.dumps({"operator_intent": {"operator_acknowledged_private_repo_secrets": True}}),
            encoding="utf-8",
        )
        assert classify_path(run / "serial.raw.log", root=root).privacy_class == PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE
        assert classify_path(run / "serial.log", root=root).privacy_class == PrivacyClass.PRIVATE_LAB_RAW_EVIDENCE
        assert classify_path(run / "serial.redacted.log", root=root).privacy_class == PrivacyClass.PUBLIC_SANITIZED
        assert classify_path(run / "serial.normalized.log", root=root).privacy_class == PrivacyClass.PUBLIC_SANITIZED
        assert not may_contain_literal_secret(run / "serial.raw.log", root=root, repo_mode="PUBLIC_RELEASE")
        assert may_contain_literal_secret(run / "serial.raw.log", root=root, repo_mode="PRIVATE_REPO")
    print("EV_PRIVACY_CLASSIFICATION_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    parser.error("no action requested")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
