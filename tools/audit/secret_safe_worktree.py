#!/usr/bin/env python3
"""Secret-safe Git worktree cleanliness gate.

The gate deliberately reports only path/status metadata. It never prints diff
hunks or file contents, because this private-lab repository may contain
intentional secrets in allowed source/raw-evidence locations and old diffs can
otherwise leak removed secret values into CI logs.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN_OUTPUT_TOKENS = ("@@", "diff --git", "+secret", "-secret")


def _run_git(root: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", *args], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def _is_worktree(root: Path) -> bool:
    proc = _run_git(root, ["rev-parse", "--is-inside-work-tree"])
    return proc.returncode == 0 and proc.stdout.strip() == "true"


def _status_lines(root: Path) -> list[str]:
    proc = _run_git(root, ["status", "--porcelain=v1", "--untracked-files=normal"])
    if proc.returncode != 0:
        return ["GIT_STATUS_FAILED"]
    lines: list[str] = []
    for raw in proc.stdout.splitlines():
        raw = raw.rstrip("\n")
        if not raw:
            continue
        # Keep only the two-column porcelain status and path metadata. Porcelain
        # output does not include line contents, but guard against accidental
        # long/odd lines by truncating to path/status metadata length.
        lines.append(raw[:300])
    return lines


def check_worktree(root: Path = ROOT, quiet: bool = False) -> int:
    root = root.resolve()
    if not _is_worktree(root):
        if not quiet:
            print("secret-safe-working-tree-clean-gate NO_GIT_WORKTREE: not inside a git worktree")
        return 77
    dirty = _status_lines(root)
    if dirty:
        if not quiet:
            print("secret-safe-working-tree-clean-gate failed: working tree has uncommitted changes")
            for line in dirty:
                print(f"WORKTREE_STATUS {line}")
        return 1
    if not quiet:
        print("secret-safe-working-tree-clean-gate passed")
    return 0


def self_test() -> None:
    test_secret = "secret-token-that-must-not-appear"
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        _run_git(root, ["init", "-q"])
        _run_git(root, ["config", "user.email", "tester@example.invalid"])
        _run_git(root, ["config", "user.name", "tester"])
        sensitive = root / "sensitive.txt"
        sensitive.write_text(f"{test_secret}\n", encoding="utf-8")
        _run_git(root, ["add", "sensitive.txt"])
        _run_git(root, ["commit", "-q", "-m", "base"])
        sensitive.write_text("<REDACTED>\n", encoding="utf-8")
        proc = subprocess.run(
            [sys.executable, __file__, "--root", str(root)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert proc.returncode == 1, proc.stdout
        assert "sensitive.txt" in proc.stdout, proc.stdout
        assert test_secret not in proc.stdout, proc.stdout
        assert "@@" not in proc.stdout, proc.stdout
        assert "diff --git" not in proc.stdout, proc.stdout
        _run_git(root, ["add", "sensitive.txt"])
        _run_git(root, ["commit", "-q", "-m", "redact"])
        assert check_worktree(root, quiet=True) == 0
    print("SECRET_SAFE_WORKTREE_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check git worktree cleanliness without printing diff contents.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    return check_worktree(args.root)


if __name__ == "__main__":
    raise SystemExit(main())
