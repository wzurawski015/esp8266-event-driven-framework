#!/usr/bin/env python3
"""Secret-safe release archive content scope guard.

Rejects accidental nested repository snapshots such as top-level ``orig/`` or
``repo/``.  The gate reports only forbidden top-level directory names, never file
contents or nested paths that might include private evidence names.
"""
from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path

FORBIDDEN_TOP_LEVEL_DIRS = {"orig", "repo"}


def _git_tracked_top_levels(root: Path) -> set[str] | None:
    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "ls-tree", "-r", "--name-only", "HEAD"],
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError):
        return None
    out: set[str] = set()
    for raw in completed.stdout.splitlines():
        if raw.strip():
            out.add(raw.split("/", 1)[0])
    return out


def check(root: Path) -> list[str]:
    root = root.resolve()
    tracked = _git_tracked_top_levels(root)
    if tracked is not None:
        found = sorted(FORBIDDEN_TOP_LEVEL_DIRS & tracked)
    else:
        found = sorted(name for name in FORBIDDEN_TOP_LEVEL_DIRS if (root / name).exists())
    return [f"forbidden top-level archive tree {name!r}" for name in found]


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        assert check(root) == []
        (root / "repo").mkdir()
        failures = check(root)
        assert failures and "repo" in failures[0]
    print("RELEASE_ARCHIVE_CONTENT_SCOPE_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check release archive content scope.")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    failures = check(args.root)
    if failures:
        for item in failures:
            print(f"RELEASE_ARCHIVE_CONTENT_SCOPE_FAILURE {item}")
        print(f"release-archive-content-scope-gate FAIL failures={len(failures)}")
        return 1
    print("release-archive-content-scope-gate PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
