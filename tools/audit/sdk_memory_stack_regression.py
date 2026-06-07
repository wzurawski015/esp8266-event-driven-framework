#!/usr/bin/env python3
"""SDK memory/stack regression gate wrapper.

The first production implementation delegates parsing to the existing SDK memory
matrix artifacts and gives honest ENVIRONMENT_BLOCKED when no fresh SDK logs are
available. It is intentionally non-secret-bearing and never prints build flags.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOG_ROOT = ROOT / "logs" / "sdk"
JSONL = ROOT / "docs" / "release" / "sdk_build_matrix.jsonl"


def _evaluate_rows(root: Path = ROOT) -> tuple[str, str, int]:
    jsonl = root / JSONL.relative_to(ROOT)
    if not jsonl.is_file():
        return "ENVIRONMENT_BLOCKED", "SDK matrix JSONL is not available", 0
    rows = []
    for raw in jsonl.read_text(encoding="utf-8", errors="ignore").splitlines():
        if raw.strip():
            rows.append(json.loads(raw))
    real_rows = [r for r in rows if r.get("status") not in {"NOT_APPLICABLE", "NOT_RUN"}]
    if not real_rows:
        return "ENVIRONMENT_BLOCKED", "no real SDK build rows available", len(rows)
    failing = [r for r in real_rows if r.get("status") != "PASS"]
    if failing:
        return "FAIL", "one or more SDK rows failed build/warning policy", len(real_rows)
    return "PASS", "SDK build matrix rows are PASS; memory details checked by sdk-memory-release-gate when artifacts exist", len(real_rows)


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "docs" / "release").mkdir(parents=True)
        assert _evaluate_rows(root)[0] == "ENVIRONMENT_BLOCKED"
        (root / "docs" / "release" / "sdk_build_matrix.jsonl").write_text(
            json.dumps({"target": "t1", "status": "PASS"}) + "\n",
            encoding="utf-8",
        )
        assert _evaluate_rows(root)[0] == "PASS"
        (root / "docs" / "release" / "sdk_build_matrix.jsonl").write_text(
            json.dumps({"target": "t1", "status": "FAIL"}) + "\n",
            encoding="utf-8",
        )
        assert _evaluate_rows(root)[0] == "FAIL"
    print("SDK_MEMORY_STACK_REGRESSION_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check SDK memory/stack regression evidence status.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    status, reason, rows = _evaluate_rows(args.root.resolve())
    print(f"sdk-memory-stack-regression-gate {status} rows={rows} reason={reason}")
    if status == "PASS":
        return 0
    if status == "ENVIRONMENT_BLOCKED":
        return 77
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
