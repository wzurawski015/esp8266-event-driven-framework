#!/usr/bin/env python3
"""Check SDK workflows use marker-compatible build commands with strict warning policy."""
from __future__ import annotations

import argparse
import re
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW_PATHS = (
    ROOT / ".github" / "workflows" / "ci.yml",
    ROOT / ".github" / "workflows" / "sdk-matrix.yml",
)
STRICT_POLICY_RE = re.compile(r"sdk_warning_policy\.py[^\n]*--strict-build-session")
SDK_BUILD_RE = re.compile(r"\.\/tools\/fw\s+sdk-build(?:\s|$)")
SDK_BUILD_ONE_RE = re.compile(r"\.\/tools\/fw\s+sdk-build-one\b")


def _has_unsafe_strict_pair(text: str) -> bool:
    """Return true when a run block pairs plain sdk-build with strict parser."""
    blocks = re.split(r"\n\s*-\s+name:\s+", "\n" + text)
    for block in blocks:
        if STRICT_POLICY_RE.search(block) and SDK_BUILD_RE.search(block) and not SDK_BUILD_ONE_RE.search(block):
            return True
    return False


def collect_failures(root: Path = ROOT) -> list[str]:
    failures: list[str] = []
    for path in (root / ".github" / "workflows").glob("*.yml"):
        text = path.read_text(encoding="utf-8", errors="ignore")
        rel = path.relative_to(root).as_posix()
        if _has_unsafe_strict_pair(text):
            failures.append(f"{rel}: strict sdk_warning_policy.py is paired with plain ./tools/fw sdk-build; use sdk-build-one or markers")
        if "sdk_warning_policy.py" in text and "--strict-build-session" in text and "--target" not in text:
            failures.append(f"{rel}: strict sdk warning policy should specify --target for deterministic marker selection")
    return failures


def self_test() -> None:
    good = """
name: ci
jobs:
  sdk:
    steps:
      - name: Build
        run: |
          ./tools/fw sdk-build-one esp8266_generic_dev 2>&1 | tee build/sdk.log
          python3 tools/audit/sdk_warning_policy.py --project-only --latest-build-session --session-kind build --strict-build-session --target esp8266_generic_dev build/sdk.log
"""
    bad = """
name: ci
jobs:
  sdk:
    steps:
      - name: Build
        run: |
          ./tools/fw sdk-build 2>&1 | tee build/sdk.log
          python3 tools/audit/sdk_warning_policy.py --project-only --latest-build-session --session-kind build --strict-build-session build/sdk.log
"""
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        wf = root / ".github" / "workflows"
        wf.mkdir(parents=True)
        (wf / "ci.yml").write_text(good, encoding="utf-8")
        assert collect_failures(root) == []
        (wf / "ci.yml").write_text(bad, encoding="utf-8")
        failures = collect_failures(root)
        assert failures, failures
        assert "sdk-build" in failures[0]
    print("SDK_BUILD_WORKFLOW_CONTRACT_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate SDK build workflow marker contract.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    failures = collect_failures(args.root.resolve())
    if failures:
        for failure in failures:
            print(f"SDK_BUILD_WORKFLOW_CONTRACT_FAILURE {failure}")
        print(f"sdk-build-workflow-contract-gate failed failures={len(failures)}")
        return 1
    print("sdk-build-workflow-contract-gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
