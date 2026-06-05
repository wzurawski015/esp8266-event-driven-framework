#!/usr/bin/env python3
"""Audit ESP8266 software-I2C speed policy and bit timing contract."""
from __future__ import annotations

import argparse
import re
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ADAPTER = ROOT / "adapters/esp8266_rtos_sdk/components/ev_platform/ev_i2c_adapter.c"
DOC = ROOT / "docs/hil/i2c-logic-analyzer-evidence-contract.md"


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="ignore")


def check_text(adapter_text: str, doc_text: str) -> list[str]:
    failures: list[str] = []
    if "EV_ESP8266_I2C_SPEED_SAFE_HZ 100000U" not in adapter_text:
        failures.append("missing 100 kHz safe speed definition")
    if "EV_ESP8266_I2C_SPEED_FAST_HZ 400000U" not in adapter_text:
        failures.append("missing 400 kHz fast speed definition")
    if "EV_ESP8266_I2C_FAST_MODE_LOGIC_ANALYZER_EVIDENCE" not in adapter_text:
        failures.append("missing fast-mode logic analyzer compile-time guard")
    if "EV_ESP8266_I2C_TARGET_SPEED_HZ == 0U" not in adapter_text:
        failures.append("missing compile-time guard for zero I2C target speed")
    if "EV_ESP8266_I2C_TURBO_LAB_ONLY" not in adapter_text:
        failures.append("missing lab-only guard for speeds above 400 kHz")
    if "EV_ESP8266_I2C_TARGET_HALF_PERIOD_US" not in adapter_text or "EV_ESP8266_I2C_HALF_PERIOD_US" not in adapter_text:
        failures.append("I2C half-period is not derived from target speed")
    if "500000U / EV_ESP8266_I2C_TARGET_SPEED_HZ" not in adapter_text:
        failures.append("missing target-speed to half-period formula")
    for token in ("100 kHz", "400 kHz", "final NACK", "SDA stuck-low recovery"):
        if token not in doc_text:
            failures.append(f"logic analyzer contract missing {token!r}")
    return failures


def check(root: Path = ROOT) -> list[str]:
    return check_text(_read(root / ADAPTER.relative_to(ROOT)), _read(root / DOC.relative_to(ROOT)))


def self_test() -> None:
    good_adapter = """
#define EV_ESP8266_I2C_SPEED_SAFE_HZ 100000U
#define EV_ESP8266_I2C_SPEED_FAST_HZ 400000U
#if EV_ESP8266_I2C_TARGET_SPEED_HZ == 0U
#error x
#endif
#if (EV_ESP8266_I2C_TARGET_SPEED_HZ > EV_ESP8266_I2C_SPEED_SAFE_HZ) && !defined(EV_ESP8266_I2C_FAST_MODE_LOGIC_ANALYZER_EVIDENCE)
#error x
#endif
#if (EV_ESP8266_I2C_TARGET_SPEED_HZ > EV_ESP8266_I2C_SPEED_FAST_HZ) && !defined(EV_ESP8266_I2C_TURBO_LAB_ONLY)
#error x
#endif
#define EV_ESP8266_I2C_TARGET_HALF_PERIOD_US (500000U / EV_ESP8266_I2C_TARGET_SPEED_HZ)
#define EV_ESP8266_I2C_HALF_PERIOD_US ((EV_ESP8266_I2C_TARGET_HALF_PERIOD_US > 0U) ? EV_ESP8266_I2C_TARGET_HALF_PERIOD_US : 1U)
"""
    good_doc = "100 kHz 400 kHz final NACK SDA stuck-low recovery"
    assert check_text(good_adapter, good_doc) == []
    assert any("derived" in item for item in check_text(good_adapter.replace("EV_ESP8266_I2C_TARGET_HALF_PERIOD_US", "EV_ESP8266_I2C_STATIC_HALF_PERIOD"), good_doc))
    assert any("400" in item for item in check_text(good_adapter.replace("EV_ESP8266_I2C_SPEED_FAST_HZ 400000U", ""), good_doc))
    assert any("zero" in item for item in check_text(good_adapter.replace("EV_ESP8266_I2C_TARGET_SPEED_HZ == 0U", ""), good_doc))
    assert any("lab-only" in item for item in check_text(good_adapter.replace("EV_ESP8266_I2C_TURBO_LAB_ONLY", ""), good_doc))
    print("I2C_SPEED_POLICY_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check ESP8266 I2C speed policy contract.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    failures = check(args.root.resolve())
    if failures:
        for failure in failures:
            print(f"I2C_SPEED_POLICY_FAILURE {failure}")
        print(f"i2c-speed-policy-gate failed failures={len(failures)}")
        return 1
    print("i2c-speed-policy-gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
