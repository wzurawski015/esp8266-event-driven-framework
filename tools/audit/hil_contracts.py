#!/usr/bin/env python3
"""Self-tests for textual HIL evidence contracts."""
from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
I2C_DOC = ROOT / "docs/hil/i2c-logic-analyzer-evidence-contract.md"
ONEWIRE_DOC = ROOT / "docs/hil/onewire-ds18b20-hil-contract.md"


def require_tokens(path: Path, tokens: tuple[str, ...], label: str) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="ignore") if path.is_file() else ""
    failures = []
    if not text:
        failures.append(f"missing document {path.relative_to(ROOT).as_posix()}")
    for token in tokens:
        if token not in text:
            failures.append(f"{label} missing token {token!r}")
    return failures


def i2c_logic_analyzer_self_test(root: Path = ROOT) -> list[str]:
    return require_tokens(root / I2C_DOC.relative_to(ROOT), (
        "100 kHz",
        "400 kHz",
        "START",
        "final NACK",
        "missing-address NACK",
        "SDA stuck-low recovery",
        "EV_HIL_I2C_SCAN_NACK_POLICY",
    ), "i2c-logic-analyzer-contract")


def onewire_timing_self_test(root: Path = ROOT) -> list[str]:
    return require_tokens(root / ONEWIRE_DOC.relative_to(ROOT), (
        "EV_HIL_ONEWIRE_PIN_MAP",
        "EV_HIL_ONEWIRE_TIMING",
        "EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on",
        "wifi=off",
        "ENVIRONMENT_BLOCKED",
        "scratchpad CRC",
    ), "onewire-timing-contract")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check HIL textual evidence contract docs.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--i2c-logic-analyzer-self-test", action="store_true")
    parser.add_argument("--onewire-timing-self-test", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    failures: list[str] = []
    label = "hil-contract"
    if args.i2c_logic_analyzer_self_test:
        label = "I2C_LOGIC_ANALYZER_CONTRACT_SELF_TEST"
        failures.extend(i2c_logic_analyzer_self_test(root))
    if args.onewire_timing_self_test:
        label = "ONEWIRE_TIMING_CONTRACT_SELF_TEST"
        failures.extend(onewire_timing_self_test(root))
    if not args.i2c_logic_analyzer_self_test and not args.onewire_timing_self_test:
        parser.error("select a self-test")
    if failures:
        for failure in failures:
            print(f"HIL_CONTRACT_FAILURE {failure}")
        print(f"{label} FAIL failures={len(failures)}")
        return 1
    print(f"{label} PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
