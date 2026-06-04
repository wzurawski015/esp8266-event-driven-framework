#!/usr/bin/env python3
"""Validate private-repo secret containment without printing secrets.

This checker is deliberately not a binary "no secrets anywhere" policy.  In
PRIVATE_REPO mode the owner may track explicit BSP-local secret sources and
explicit raw/private lab evidence.  Sanitized evidence, reports, generated
artifacts and ordinary source files remain hard-fail locations for copied secret
values.
"""
from __future__ import annotations

import argparse
import ast
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_privacy_classification import (  # noqa: E402
    ALLOWED_LOCAL_SECRET_PATHS,
    PrivacyClass,
    classify_path,
    may_contain_literal_secret,
)

SECRET_MACRO_RE = re.compile(
    r"^\s*#\s*define\s+(EV_BOARD_NET_[A-Z0-9_]+)\s+"
    r"(?P<value>\"(?:\\.|[^\"])*\"|[^\s/]+)"
)
BOARD_SECRETS_LOCAL = "board_secrets.local.h"
SENSITIVE_SECRET_MACROS = {
    "EV_BOARD_NET_WIFI_SSID",
    "EV_BOARD_NET_WIFI_PASSWORD",
    "EV_BOARD_NET_COMMAND_TOKEN",
}
TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".py",
    ".md",
    ".txt",
    ".json",
    ".jsonl",
    ".log",
    ".def",
    ".mk",
    ".cmake",
    ".yml",
    ".yaml",
    ".ini",
    ".cfg",
    ".profile",
    ".defaults",
    ".patch",
    ".diff",
}
TEXT_FILENAMES = {"Makefile", "component.mk", ".gitignore", ".dockerignore", "Doxyfile", "sha256sums.txt"}
IGNORED_DIR_NAMES = {
    ".git",
    "__pycache__",
    ".pytest_cache",
    ".mypy_cache",
    ".ruff_cache",
    ".cache",
    "build",
    "out",
    "dist",
    "logs",
    "log",
    "docs/generated",
}
PLACEHOLDER_FRAGMENTS = (
    "<redacted>",
    "redacted",
    "your_",
    "your-",
    "your ",
    "placeholder",
    "change_me",
    "changeme",
    "change-me",
    "example",
    "dummy",
)
PLACEHOLDER_VALUES = {
    "",
    "ssid",
    "wifi_ssid",
    "wifi-password",
    "wifi_password",
    "password",
    "command-token",
    "command_token",
    "token",
    "none",
    "null",
}


@dataclass(frozen=True)
class SecretValue:
    rel: str
    line: int
    macro: str
    value: str


def normalize_mode(mode: str | None) -> str:
    value = (mode or os.environ.get("EV_REPO_MODE") or "").strip().upper().replace("-", "_")
    if value in {"", "PRIVATE", "PRIVATE_REPO", "PRIVATE_REPO_MODE"}:
        return "PRIVATE_REPO"
    if value in {"PUBLIC", "PUBLIC_RELEASE", "PUBLIC_RELEASE_MODE"}:
        return "PUBLIC_RELEASE"
    raise ValueError(f"unsupported repo mode: {mode}")


def relpath(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def is_allowed_local_secret_path(rel: str) -> bool:
    return rel in ALLOWED_LOCAL_SECRET_PATHS


def is_placeholder(value: str) -> bool:
    normalized = value.strip().strip('"').strip().lower()
    if normalized in PLACEHOLDER_VALUES:
        return True
    return any(fragment in normalized for fragment in PLACEHOLDER_FRAGMENTS)


def c_string_value(token: str) -> str | None:
    token = token.strip()
    if not (token.startswith('"') and token.endswith('"')):
        return None
    try:
        value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return None
    return value if isinstance(value, str) else None


def parse_secret_macros(path: Path, root: Path) -> list[SecretValue]:
    values: list[SecretValue] = []
    try:
        text = path.read_text(encoding="utf-8", errors="ignore")
    except OSError:
        return values
    for line_no, raw in enumerate(text.splitlines(), 1):
        match = SECRET_MACRO_RE.match(raw)
        if match is None:
            continue
        string_value = c_string_value(match.group("value"))
        macro = match.group(1)
        if string_value is None or macro not in SENSITIVE_SECRET_MACROS:
            continue
        values.append(SecretValue(relpath(path, root), line_no, macro, string_value))
    return values


def is_ignored_dir(path: Path, root: Path) -> bool:
    rel = path.relative_to(root).as_posix()
    return path.name in IGNORED_DIR_NAMES or rel in IGNORED_DIR_NAMES


def iter_repo_files(root: Path) -> Iterable[Path]:
    for dirpath, dirnames, filenames in os.walk(root):
        current = Path(dirpath)
        dirnames[:] = [name for name in dirnames if not is_ignored_dir(current / name, root)]
        for filename in filenames:
            yield current / filename


def should_scan_as_text(path: Path) -> bool:
    if path.name in TEXT_FILENAMES or path.suffix in TEXT_SUFFIXES:
        return True
    try:
        chunk = path.read_bytes()[:2048]
    except OSError:
        return False
    return b"\0" not in chunk


def line_number_for_value(text: str, value: str) -> int:
    index = text.find(value)
    if index < 0:
        return 1
    return text.count("\n", 0, index) + 1


def _mode_allows_path(path: Path, root: Path, repo_mode: str) -> bool:
    return may_contain_literal_secret(path, root=root, repo_mode=repo_mode)


def validate_private_repo_secrets_policy(root: Path, repo_mode: str = "PRIVATE_REPO") -> list[str]:
    repo_mode = normalize_mode(repo_mode)
    errors: list[str] = []
    all_macro_values: list[SecretValue] = []

    for path in iter_repo_files(root):
        rel = relpath(path, root)
        if path.name == BOARD_SECRETS_LOCAL:
            classified = classify_path(path, root=root, repo_mode=repo_mode)
            if classified.privacy_class != PrivacyClass.PRIVATE_LAB_SECRET_SOURCE:
                errors.append(f"SECRET_FILE_OUTSIDE_BSP_ALLOWLIST {rel}:0 board_secrets.local.h=<REDACTED>")
        if path.is_file() and should_scan_as_text(path):
            all_macro_values.extend(parse_secret_macros(path, root))

    real_allowed_values = [
        item
        for item in all_macro_values
        if is_allowed_local_secret_path(item.rel) and not is_placeholder(item.value)
    ]

    if repo_mode == "PUBLIC_RELEASE":
        for item in real_allowed_values:
            errors.append(f"PUBLIC_RELEASE_SECRET_PRESENT {item.rel}:{item.line} {item.macro}=<REDACTED>")

    for item in all_macro_values:
        if is_placeholder(item.value):
            continue
        path = root / item.rel
        if _mode_allows_path(path, root, repo_mode):
            continue
        if item.rel.endswith(".example.h"):
            errors.append(f"REAL_SECRET_IN_EXAMPLE {item.rel}:{item.line} {item.macro}=<REDACTED>")
        else:
            klass = classify_path(path, root=root, repo_mode=repo_mode).privacy_class.value
            errors.append(f"PRIVATE_SECRET_MACRO_OUTSIDE_ALLOWLIST {item.rel}:{item.line} class={klass} {item.macro}=<REDACTED>")

    sensitive_values = [item for item in real_allowed_values if item.value != ""]
    for path in iter_repo_files(root):
        if not path.is_file() or not should_scan_as_text(path):
            continue
        rel = relpath(path, root)
        if _mode_allows_path(path, root, repo_mode):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for item in sensitive_values:
            if item.value and item.value in text:
                line_no = line_number_for_value(text, item.value)
                klass = classify_path(path, root=root, repo_mode=repo_mode).privacy_class.value
                errors.append(f"PRIVATE_SECRET_REFERENCE {rel}:{line_no} class={klass} {item.macro}=<REDACTED>")

    for path in iter_repo_files(root):
        rel = relpath(path, root)
        if path.name == BOARD_SECRETS_LOCAL and not _mode_allows_path(path, root, repo_mode):
            errors.append(f"SECRET_FILE_COPIED {rel}:0 board_secrets.local.h=<REDACTED>")

    return sorted(set(errors))


def self_test() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        allowed = root / "bsp" / "wemos_esp_wroom_02_18650"
        allowed.mkdir(parents=True)
        secret_file = allowed / BOARD_SECRETS_LOCAL
        secret_file.write_text(
            "#define EV_BOARD_NET_WIFI_SSID \"lab-ssid-value\"\n"
            "#define EV_BOARD_NET_WIFI_PASSWORD \"lab-password-value\"\n"
            "#define EV_BOARD_NET_COMMAND_TOKEN \"lab-command-token\"\n",
            encoding="utf-8",
        )
        assert validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO") == []
        public_errors = validate_private_repo_secrets_policy(root, repo_mode="PUBLIC_RELEASE")
        assert public_errors
        assert not any("lab-password-value" in error for error in public_errors)

        report_dir = root / "docs" / "release"
        report_dir.mkdir(parents=True)
        (report_dir / "report.md").write_text("password copy: lab-password-value\n", encoding="utf-8")
        leak_errors = validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO")
        assert any("PRIVATE_SECRET_REFERENCE" in error for error in leak_errors)
        assert not any("lab-password-value" in error for error in leak_errors)
        (report_dir / "report.md").write_text("password copy: <REDACTED>\n", encoding="utf-8")
        assert validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO") == []

        run = root / "docs" / "release" / "wemos_one_shot_evidence" / "wemos" / "current" / "runs" / "r1"
        run.mkdir(parents=True)
        (run / "manifest.json").write_text(
            '{"operator_intent":{"operator_acknowledged_private_repo_secrets":true}}\n',
            encoding="utf-8",
        )
        (run / "serial.raw.log").write_text("wifi:connected with lab-ssid-value, aid = 1\n", encoding="utf-8")
        (run / "serial.log").write_text("wifi:connected with lab-ssid-value, aid = 1\n", encoding="utf-8")
        assert validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO") == []
        public_raw_errors = validate_private_repo_secrets_policy(root, repo_mode="PUBLIC_RELEASE")
        assert any("PRIVATE_SECRET_REFERENCE" in error for error in public_raw_errors)
        assert not any("lab-ssid-value" in error for error in public_raw_errors)
        (run / "serial.redacted.log").write_text("wifi:connected with lab-ssid-value, aid = 1\n", encoding="utf-8")
        redacted_errors = validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO")
        assert any("serial.redacted.log" in error for error in redacted_errors)
        assert not any("lab-ssid-value" in error for error in redacted_errors)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        example = root / "bsp" / "demo"
        example.mkdir(parents=True)
        (example / "board_secrets.example.h").write_text(
            "#define EV_BOARD_NET_WIFI_SSID \"YOUR_WIFI_SSID\"\n"
            "#define EV_BOARD_NET_WIFI_PASSWORD \"<REDACTED>\"\n"
            "#define EV_BOARD_NET_COMMAND_TOKEN \"YOUR_COMMAND_TOKEN\"\n",
            encoding="utf-8",
        )
        assert validate_private_repo_secrets_policy(root, repo_mode="PRIVATE_REPO") == []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate private-repo board secret containment.")
    parser.add_argument("--root", default=str(ROOT), help="repository root to scan")
    parser.add_argument("--mode", choices=["PRIVATE_REPO", "PUBLIC_RELEASE"], default=None)
    parser.add_argument("--self-test-only", action="store_true")
    args = parser.parse_args(argv)

    self_test()
    if args.self_test_only:
        print("private repo secrets policy self-test passed")
        return 0

    try:
        repo_mode = normalize_mode(args.mode)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if os.environ.get("PUBLIC_RELEASE", "").strip() == "1":
        repo_mode = "PUBLIC_RELEASE"

    root = Path(args.root).resolve()
    errors = validate_private_repo_secrets_policy(root, repo_mode=repo_mode)
    if errors:
        for error in errors:
            print(error)
        print(f"private repo secrets policy failed mode={repo_mode} failures={len(errors)}")
        return 1
    print(f"private repo secrets policy passed mode={repo_mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
