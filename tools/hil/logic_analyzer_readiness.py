#!/usr/bin/env python3
"""Logic-analyzer evidence readiness/import gate.

This is a schema/readiness gate, not a replacement for hardware HIL. Without a
real manifest path it reports ENVIRONMENT_BLOCKED rather than PASS.  A schema
PASS means the submitted manifest is structurally suitable for review/import; it
is not a synthetic hardware PASS.
"""
from __future__ import annotations

import argparse
import json
import tempfile
from pathlib import Path
from typing import Any

REQUIRED_COMMON = {"suite", "board", "run_id", "firmware_commit", "logic_analyzer_model", "sample_rate_hz", "channels", "captures"}
REQUIRED_I2C_CAPTURES_100KHZ = {
    "start_condition",
    "address_ack",
    "write_byte",
    "repeated_start",
    "read_byte",
    "final_nack_then_stop",
    "stop_after_address_nack",
    "missing_address_nack_stop_release",
    "sda_stuck_low_recovery",
    "scl_held_low_timeout",
}
REQUIRED_I2C_FAST_FIELDS = {"measured_hz", "rise_time_ns", "fall_time_ns", "pullup_ohms", "bus_capacitance_note"}
REQUIRED_ONEWIRE_CAPTURES = {
    "onewire_reset_presence",
    "onewire_read_write_slots_wifi_on",
    "onewire_release_after_transaction",
    "ds18b20_scratchpad_crc_wifi_on",
}


def _capture_names(manifest: dict[str, Any]) -> set[str]:
    names: set[str] = set()
    for item in manifest.get("captures", []):
        if isinstance(item, dict) and isinstance(item.get("name"), str):
            names.add(item["name"])
        elif isinstance(item, str):
            names.add(item)
    return names


def _captures(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    for item in manifest.get("captures", []):
        if isinstance(item, dict):
            out.append(item)
        elif isinstance(item, str):
            out.append({"name": item})
    return out


def _target_hz(manifest: dict[str, Any]) -> int:
    for key in ("target_hz", "i2c_target_hz", "measured_hz"):
        value = manifest.get(key)
        if isinstance(value, int):
            return value
        if isinstance(value, str):
            try:
                return int(value, 0)
            except ValueError:
                pass
    return 100_000


def _field_present(value: Any) -> bool:
    if value is None:
        return False
    if isinstance(value, str):
        return bool(value.strip())
    return True


def _fast_capture_failures(manifest: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    captures = _captures(manifest)
    fast_candidates = [c for c in captures if str(c.get("name", "")).startswith("fast_400khz") or int(c.get("target_hz", 0) or 0) >= 400_000]
    manifest_fast = _target_hz(manifest) >= 400_000 or str(manifest.get("speed_profile", "")).lower() in {"fast", "400khz"}
    if not (manifest_fast or fast_candidates):
        return failures
    if not fast_candidates:
        failures.append("400 kHz evidence requires at least one fast_400khz capture")
        return failures
    for index, capture in enumerate(fast_candidates, 1):
        for field in sorted(REQUIRED_I2C_FAST_FIELDS):
            if not _field_present(capture.get(field)) and not _field_present(manifest.get(field)):
                failures.append(f"fast 400 kHz capture #{index} missing {field}")
    return failures


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
        for name in sorted(REQUIRED_I2C_CAPTURES_100KHZ - names):
            failures.append(f"missing i2c capture {name}")
        failures.extend(_fast_capture_failures(manifest))
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
        "target_hz": 100_000,
        "captures": [{"name": name} for name in sorted(REQUIRED_I2C_CAPTURES_100KHZ)],
    }
    good_i2c_fast = dict(good_i2c)
    good_i2c_fast["target_hz"] = 400_000
    good_i2c_fast["speed_profile"] = "fast"
    good_i2c_fast["captures"] = list(good_i2c["captures"]) + [
        {
            "name": "fast_400khz_final_nack_then_stop",
            "target_hz": 400_000,
            "measured_hz": 392_000,
            "rise_time_ns": 410,
            "fall_time_ns": 80,
            "pullup_ohms": 10_000,
            "bus_capacitance_note": "lab cable short, all shared devices connected",
        }
    ]
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
        "captures": [{"name": name} for name in sorted(REQUIRED_ONEWIRE_CAPTURES)],
    }
    assert validate_manifest(good_i2c) == []
    assert validate_manifest(good_i2c_fast) == []
    assert validate_manifest(good_ow) == []
    bad = dict(good_ow)
    bad["wifi_state"] = "off"
    assert any("wifi_state=on" in item for item in validate_manifest(bad))
    bad2 = dict(good_i2c)
    bad2["captures"] = [{"name": "final_nack_then_stop"}]
    assert any("stop_after_address_nack" in item for item in validate_manifest(bad2))
    bad3 = dict(good_i2c_fast)
    bad3["captures"] = list(good_i2c["captures"]) + [{"name": "fast_400khz_final_nack_then_stop", "target_hz": 400_000}]
    assert any("rise_time_ns" in item for item in validate_manifest(bad3))
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
