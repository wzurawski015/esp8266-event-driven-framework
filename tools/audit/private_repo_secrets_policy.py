#!/usr/bin/env python3
"""Validate the private-repo board-secrets policy without printing secrets.

Default mode is intentionally *not* a public no-secrets check.  This project may
track BSP-local `board_secrets.local.h` files in a private lab repository, but
secret values must not leak into release documents, generated outputs, logs or
other source files.
"""
from __future__ import annotations

import argparse
import ast
import os
import re
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
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
ALLOWED_LOCAL_SECRET_PATHS = {"bsp/wemos_esp_wroom_02_18650/board_secrets.local.h"}
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
TEXT_FILENAMES = {"Makefile", "component.mk", ".gitignore", ".dockerignore", "Doxyfile"}
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


def validate_private_repo_secrets_policy(root: Path, public_release: bool = False) -> list[str]:
    errors: list[str] = []
    local_secret_files: list[Path] = []
    all_macro_values: list[SecretValue] = []

    for path in iter_repo_files(root):
        rel = relpath(path, root)
        if path.name == BOARD_SECRETS_LOCAL:
            if not is_allowed_local_secret_path(rel):
                errors.append(f"SECRET_FILE_OUTSIDE_BSP_ALLOWLIST {rel}:0 board_secrets.local.h=<REDACTED>")
            else:
                local_secret_files.append(path)
        if path.is_file() and should_scan_as_text(path):
            all_macro_values.extend(parse_secret_macros(path, root))

    real_allowed_values = [
        item
        for item in all_macro_values
        if is_allowed_local_secret_path(item.rel) and not is_placeholder(item.value)
    ]

    if public_release:
        for item in real_allowed_values:
            errors.append(f"PUBLIC_RELEASE_SECRET_PRESENT {item.rel}:{item.line} {item.macro}=<REDACTED>")

    for item in all_macro_values:
        if is_placeholder(item.value):
            continue
        if is_allowed_local_secret_path(item.rel):
            continue
        if item.rel.endswith(".example.h"):
            errors.append(f"REAL_SECRET_IN_EXAMPLE {item.rel}:{item.line} {item.macro}=<REDACTED>")
        else:
            errors.append(f"PRIVATE_SECRET_MACRO_OUTSIDE_ALLOWLIST {item.rel}:{item.line} {item.macro}=<REDACTED>")

    sensitive_values = [item for item in real_allowed_values if item.value != ""]
    for path in iter_repo_files(root):
        rel = relpath(path, root)
        if is_allowed_local_secret_path(rel):
            continue
        if not path.is_file() or not should_scan_as_text(path):
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for item in sensitive_values:
            if item.value and item.value in text:
                line_no = line_number_for_value(text, item.value)
                errors.append(f"PRIVATE_SECRET_REFERENCE {rel}:{line_no} {item.macro}=<REDACTED>")

    # Build and generated output must never contain a copied local-secret file.
    for path in iter_repo_files(root):
        rel = relpath(path, root)
        if path.name == BOARD_SECRETS_LOCAL and not is_allowed_local_secret_path(rel):
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
        assert validate_private_repo_secrets_policy(root, public_release=False) == []
        public_errors = validate_private_repo_secrets_policy(root, public_release=True)
        assert public_errors
        assert not any("lab-password-value" in error for error in public_errors)

        report_dir = root / "docs" / "release"
        report_dir.mkdir(parents=True)
        (report_dir / "report.md").write_text("password copy: lab-password-value\n", encoding="utf-8")
        leak_errors = validate_private_repo_secrets_policy(root, public_release=False)
        assert any("PRIVATE_SECRET_REFERENCE" in error for error in leak_errors)
        assert not any("lab-password-value" in error for error in leak_errors)
        (report_dir / "report.md").write_text("password copy: <REDACTED>\n", encoding="utf-8")
        assert validate_private_repo_secrets_policy(root, public_release=False) == []

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
        assert validate_private_repo_secrets_policy(root, public_release=False) == []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Validate private-repo board secret containment.")
    parser.add_argument("--root", default=str(ROOT), help="repository root to scan")
    parser.add_argument("--self-test-only", action="store_true")
    args = parser.parse_args(argv)

    self_test()
    if args.self_test_only:
        print("private repo secrets policy self-test passed")
        return 0

    root = Path(args.root).resolve()
    public_release = os.environ.get("PUBLIC_RELEASE", "").strip() == "1"
    errors = validate_private_repo_secrets_policy(root, public_release=public_release)
    if errors:
        for error in errors:
            print(error)
        mode = "PUBLIC_RELEASE" if public_release else "PRIVATE_REPO"
        print(f"private repo secrets policy failed mode={mode} failures={len(errors)}")
        return 1
    mode = "PUBLIC_RELEASE" if public_release else "PRIVATE_REPO"
    print(f"private repo secrets policy passed mode={mode}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
