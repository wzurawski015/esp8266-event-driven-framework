#!/usr/bin/env python3
"""Secret-safe release archive workflow contract gate.

The gate is deliberately textual and conservative: it checks that release docs
and tools point operators at ./tools/fw release-archive and that the wrapper runs
release-prearchive-gate before invoking git archive.  It never prints diffs.
"""
from __future__ import annotations

import argparse
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOOLS_FW = ROOT / "tools" / "fw"
DOC = ROOT / "docs" / "release" / "release-archive-discipline.md"
MAKEFILE = ROOT / "Makefile"
REQUIRED_PREARCHIVE_DEPS = (
    "host-test",
    "property-test",
    "host-strict-test",
    "host-sanitize-test",
    "route-registry-integration-gate",
)


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore") if path.is_file() else ""


def check(root: Path = ROOT) -> list[str]:
    failures: list[str] = []
    fw = _read(root / TOOLS_FW.relative_to(ROOT))
    doc = _read(root / DOC.relative_to(ROOT))
    makefile = _read(root / MAKEFILE.relative_to(ROOT))
    if "release_archive()" not in fw:
        failures.append("tools/fw missing release_archive() wrapper")
    release_fn = fw[fw.find("release_archive()"):fw.find("sdk_check()") if "sdk_check()" in fw else len(fw)]
    if "make -C \"$ROOT_DIR\" release-prearchive-gate" not in release_fn:
        failures.append("release_archive() does not run release-prearchive-gate")
    if "git -C \"$ROOT_DIR\" archive" not in release_fn:
        failures.append("release_archive() does not own git archive invocation")
    if "git diff" in release_fn:
        failures.append("release_archive() must not print raw git diff output")
    prearchive_line = ""
    for line in makefile.splitlines():
        if line.startswith("release-prearchive-gate:"):
            prearchive_line = line
            break
    if not prearchive_line:
        failures.append("Makefile missing release-prearchive-gate target")
    else:
        for dep in REQUIRED_PREARCHIVE_DEPS:
            if dep not in prearchive_line.split():
                failures.append(f"release-prearchive-gate missing required host-quality dependency {dep}")
    if "./tools/fw release-archive" not in doc:
        failures.append("operator documentation does not name ./tools/fw release-archive")
    if "release-prearchive-gate" not in doc:
        failures.append("operator documentation does not require release-prearchive-gate")
    if "Do not use a raw `git archive" not in doc and "raw `git archive" not in doc:
        failures.append("operator documentation does not demote raw git archive to low-level fallback")
    for dep in REQUIRED_PREARCHIVE_DEPS:
        if dep not in doc:
            failures.append(f"operator documentation does not describe {dep} as a prearchive requirement")
    return failures


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "tools").mkdir()
        (root / "docs" / "release").mkdir(parents=True)
        (root / "Makefile").write_text(
            "release-prearchive-gate: evidence-redaction-check host-test property-test host-strict-test host-sanitize-test route-registry-integration-gate\n",
            encoding="utf-8",
        )
        (root / "tools" / "fw").write_text(
            'release_archive() {\n'
            '  make -C "$ROOT_DIR" release-prearchive-gate\n'
            '  git -C "$ROOT_DIR" archive --format=tar.gz -o out.tgz HEAD\n'
            '}\n'
            'sdk_check() { :; }\n',
            encoding="utf-8",
        )
        (root / "docs" / "release" / "release-archive-discipline.md").write_text(
            "Use ./tools/fw release-archive after release-prearchive-gate. Do not use a raw `git archive ... HEAD` as the normal release path. "
            "The prearchive gate requires host-test, property-test, host-strict-test, host-sanitize-test and route-registry-integration-gate.\n",
            encoding="utf-8",
        )
        assert check(root) == []
        (root / "tools" / "fw").write_text('release_archive() { git diff --exit-code; git archive HEAD; }\n', encoding="utf-8")
        assert check(root)
    print("RELEASE_ARCHIVE_WORKFLOW_CONTRACT_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check release archive workflow discipline.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    failures = check(args.root.resolve())
    if failures:
        for failure in failures:
            print(f"RELEASE_ARCHIVE_WORKFLOW_CONTRACT_FAILURE {failure}")
        print(f"release-archive-workflow-contract-gate FAIL failures={len(failures)}")
        return 1
    print("release-archive-workflow-contract-gate PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
