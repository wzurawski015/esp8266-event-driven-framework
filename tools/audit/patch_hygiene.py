#!/usr/bin/env python3
"""Fast patch hygiene gate for whitespace and dirty-diff mistakes."""
from __future__ import annotations

import argparse
import shutil
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
TEXT_SUFFIXES = {"", ".c", ".h", ".cc", ".cpp", ".hpp", ".py", ".sh", ".mk", ".cmake", ".yml", ".yaml", ".json", ".md", ".txt", ".def", ".profile", ".defaults", ".dockerignore", ".gitignore", ".editorconfig"}
SOURCE_ROOTS = (".github", "actors", "adapters", "apps", "bsp", "config", "core", "diag", "domain", "drivers", "modules", "ports", "runtime", "tests", "tools", "docs/architecture", "docs/hil", "docs/perf", "docs/security", "docs/specs")
SOURCE_FILES = ("Makefile", "README.md", "CONTRIBUTING.md", "Doxyfile", ".editorconfig", ".gitignore", ".dockerignore")

@dataclass(frozen=True)
class WhitespaceIssue:
    rel: str
    line: int
    kind: str


def _rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def _is_ignored(path: Path, root: Path) -> bool:
    rel = _rel(path, root) if path.is_relative_to(root) else path.as_posix()
    parts = rel.split("/")
    if set(parts) & {".git", "__pycache__", ".pytest_cache"}:
        return True
    if rel.startswith("build/") or rel.startswith("docs/generated/") or rel.startswith("docs/release/"):
        return True
    # ESP8266 RTOS SDK target builds are generated artefacts. They may contain
    # vendor-generated Make fragments with trailing whitespace; patch hygiene
    # must check project sources, not generated SDK build trees.
    if (
        len(parts) >= 5
        and parts[0] == "adapters"
        and parts[1] == "esp8266_rtos_sdk"
        and parts[2] == "targets"
        and "build" in parts[4:]
    ):
        return True
    if (
        len(parts) == 5
        and parts[0] == "adapters"
        and parts[1] == "esp8266_rtos_sdk"
        and parts[2] == "targets"
        and parts[4] == "sdkconfig"
    ):
        return True
    return False


def _looks_text(path: Path) -> bool:
    if path.name in {"Makefile", "Doxyfile", ".editorconfig", ".gitignore", ".dockerignore"}:
        return True
    return path.suffix.lower() in TEXT_SUFFIXES


def iter_candidate_files(root: Path) -> Iterable[Path]:
    for name in SOURCE_FILES:
        path = root / name
        if path.is_file():
            yield path
    for rel_root in SOURCE_ROOTS:
        base = root / rel_root
        if not base.exists():
            continue
        for path in base.rglob("*"):
            if path.is_file() and not _is_ignored(path, root) and _looks_text(path):
                yield path


def find_trailing_whitespace(root: Path = ROOT) -> list[WhitespaceIssue]:
    issues: list[WhitespaceIssue] = []
    for path in iter_candidate_files(root):
        try:
            data = path.read_bytes()
        except OSError:
            continue
        if b"\0" in data:
            continue
        text = data.decode("utf-8", errors="ignore")
        for line_no, raw in enumerate(text.splitlines(), 1):
            if raw.endswith(" ") or raw.endswith("\t"):
                issues.append(WhitespaceIssue(_rel(path, root), line_no, "trailing-whitespace"))
    return issues


def inside_git_worktree(root: Path) -> bool | None:
    git = shutil.which("git")
    if git is None:
        return None
    proc = subprocess.run([git, "rev-parse", "--is-inside-work-tree"], cwd=root, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return proc.returncode == 0 and proc.stdout.strip() == "true"


def run_git_diff_check(root: Path) -> int:
    # Phase 7: never run `git diff --check` here because Git may print line
    # contents. The explicit scanner below reports only path:line:reason and
    # excludes historical release evidence. Missing Git must not produce a
    # Python traceback; the host validation image still installs Git because
    # worktree gates need it.
    worktree = inside_git_worktree(root)
    if worktree is None:
        print("patch-hygiene-gate git-diff-check NO_GIT_BINARY")
    elif not worktree:
        print("patch-hygiene-gate git-diff-check NO_GIT_WORKTREE")
    return 0


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "tools").mkdir(parents=True)
        (root / "docs" / "release").mkdir(parents=True)
        (root / "tools" / "good.py").write_text("print('ok')\n", encoding="utf-8")
        (root / "tools" / "bad.py").write_text("print('bad')  \n", encoding="utf-8")
        (root / "docs" / "release" / "legacy.log").write_text("legacy evidence  \n", encoding="utf-8")
        generated = root / "adapters" / "esp8266_rtos_sdk" / "targets" / "wemos_esp_wroom_02_18650" / "build" / "esp_common"
        generated.mkdir(parents=True)
        (generated / "component_project_vars.mk").write_text("generated := yes  \n", encoding="utf-8")
        issues = find_trailing_whitespace(root)
        assert any(issue.rel == "tools/bad.py" for issue in issues)
        assert not any(issue.rel.startswith("docs/release/") for issue in issues)
        assert not any("/build/" in f"/{issue.rel}/" for issue in issues)
    print("PATCH_HYGIENE_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check patch hygiene without rewriting files.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    root = args.root.resolve()
    git_rc = run_git_diff_check(root)
    issues = find_trailing_whitespace(root)
    if issues:
        for issue in issues:
            print(f"PATCH_HYGIENE_TRAILING_WHITESPACE {issue.rel}:{issue.line} {issue.kind}")
        print(f"patch-hygiene-gate failed trailing_whitespace={len(issues)}")
        return 1
    print("patch-hygiene-gate passed")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
