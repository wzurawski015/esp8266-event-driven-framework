#!/usr/bin/env python3
"""Logic-analyzer evidence readiness/import gate.

This is a schema/readiness gate, not a replacement for hardware HIL. Without a
real manifest path it reports ENVIRONMENT_BLOCKED rather than PASS.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path
from typing import Any

REQUIRED_COMMON = {"suite", "board", "run_id", "firmware_commit", "logic_analyzer_model", "sample_rate_hz", "channels", "captures"}
REQUIRED_I2C_CAPTURES = {"stop_after_address_nack", "final_nack_then_stop", "sda_stuck_low_recovery"}
REQUIRED_ONEWIRE_CAPTURES = {"onewire_reset_presence", "onewire_read_write_slots_wifi_on"}


def _capture_names(manifest: dict[str, Any]) -> set[str]:
    names: set[str] = set()
    for item in manifest.get("captures", []):
        if isinstance(item, dict) and isinstance(item.get("name"), str):
            names.add(item["name"])
        elif isinstance(item, str):
            names.add(item)
    return names


def validate_manifest(manifest: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    missing = sorted(REQUIRED_COMMON - set(manifest))
    for key in missing:
        failures.append(f"missing required field {key}")
    suite = str(manifest.get("suite", ""))
    if suite not in {"i2c", "onewire"}:
        failures.append("suite must be i2c or onewire")
    sample_rate = manifest.get("sample_rate_hz")
    if not isinstance(sample_rate, int) or sample_rate <= 0:
        failures.append("sample_rate_hz must be a positive integer")
    channels = manifest.get("channels")
    if not isinstance(channels, dict):
        failures.append("channels must be an object")
    else:
        if suite == "i2c" and not {"scl", "sda"}.issubset(channels):
            failures.append("i2c channels must include scl and sda")
        if suite == "onewire" and "dq" not in channels:
            failures.append("onewire channels must include dq")
    names = _capture_names(manifest)
    if suite == "i2c":
        for name in sorted(REQUIRED_I2C_CAPTURES - names):
            failures.append(f"missing i2c capture {name}")
    if suite == "onewire":
        for name in sorted(REQUIRED_ONEWIRE_CAPTURES - names):
            failures.append(f"missing onewire capture {name}")
        if manifest.get("wifi_state") != "on":
            failures.append("onewire logic analyzer evidence requires wifi_state=on")
    return failures


def load_manifest(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def self_test() -> None:
    good_i2c = {
        "suite": "i2c",
        "board": "atnel_air_esp_motherboard",
        "run_id": "self-test",
        "operator": "<REDACTED>",
        "firmware_commit": "abcdef0",
        "logic_analyzer_model": "example-analyzer",
        "sample_rate_hz": 24_000_000,
        "channels": {"scl": 0, "sda": 1},
        "captures": [
            {"name": "stop_after_address_nack"},
            {"name": "final_nack_then_stop"},
            {"name": "sda_stuck_low_recovery"},
        ],
    }
    good_ow = {
        "suite": "onewire",
        "board": "atnel_air_esp_motherboard",
        "run_id": "self-test",
        "operator": "<REDACTED>",
        "firmware_commit": "abcdef0",
        "logic_analyzer_model": "example-analyzer",
        "sample_rate_hz": 24_000_000,
        "wifi_state": "on",
        "channels": {"dq": 0},
        "captures": [{"name": "onewire_reset_presence"}, {"name": "onewire_read_write_slots_wifi_on"}],
    }
    assert validate_manifest(good_i2c) == []
    assert validate_manifest(good_ow) == []
    bad = dict(good_ow)
    bad["wifi_state"] = "off"
    assert any("wifi_state=on" in item for item in validate_manifest(bad))
    bad2 = dict(good_i2c)
    bad2["captures"] = [{"name": "final_nack_then_stop"}]
    assert any("stop_after_address_nack" in item for item in validate_manifest(bad2))
    with tempfile.TemporaryDirectory() as td:
        p = Path(td) / "manifest.json"
        p.write_text(json.dumps(good_i2c), encoding="utf-8")
        assert validate_manifest(load_manifest(p)) == []
    print("HIL_LOGIC_ANALYZER_READINESS_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate logic-analyzer HIL evidence manifest readiness.")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.manifest is None:
        print("HIL_LOGIC_ANALYZER_READINESS ENVIRONMENT_BLOCKED: EV_HIL_LOGIC_ANALYZER_MANIFEST not set")
        return 77
    try:
        manifest = load_manifest(args.manifest)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"HIL_LOGIC_ANALYZER_READINESS FAIL: manifest unreadable or invalid JSON: {exc}")
        return 1
    failures = validate_manifest(manifest)
    if failures:
        for failure in failures:
            print(f"HIL_LOGIC_ANALYZER_MANIFEST_FAILURE {failure}")
        print(f"HIL_LOGIC_ANALYZER_READINESS FAIL failures={len(failures)}")
        return 1
    print("HIL_LOGIC_ANALYZER_READINESS PASS manifest_schema=PASS real_hil_status=MANIFEST_ONLY")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
