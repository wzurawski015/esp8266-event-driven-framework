#!/usr/bin/env python3
"""Validate executable board-wiring truth contracts for ATNEL bus HIL."""
from __future__ import annotations

import argparse
import re
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIN_RE = re.compile(r"EV_BSP_PIN\((?P<name>[A-Z0-9_]+),\s*(?P<gpio>[0-9]+)U")

ATNEL_BOARD = "bsp/atnel_air_esp_motherboard"
EXPECTED_ATNEL_PINS = {
    "PIN_I2C0_SCL": 4,
    "PIN_I2C0_SDA": 5,
    "PIN_ONEWIRE0_DQ": 12,
}
ATB_BOARD = "bsp/atb_thermo_wemos_esp_wroom_02_4mb"
EXPECTED_ATB_PINS = {
    "PIN_I2C0_SCL": 5,
    "PIN_I2C0_SDA": 4,
    "PIN_ONEWIRE0_DQ": 13,
    "PIN_PWR_CTRL": 0,
    "PIN_STATUS_LED": 2,
    "PIN_PIR_CHECK": 14,
    "PIN_AUDIO": 12,
    "PIN_PIR_DIS": 15,
}
REQUIRED_DOCS = (
    "docs/hil/atnel-board-wiring-evidence-contract.md",
    "docs/hil/i2c-logic-analyzer-evidence-contract.md",
    "docs/hil/atb-thermo-hardware-evidence-contract.md",
)
REQUIRED_I2C_HIL_TOKENS = (
    "EV_HIL_BOARD_PIN_MAP",
    "EV_HIL_I2C_RECOVERY_EVIDENCE",
    "EV_HIL_I2C_MUTEX_EVIDENCE",
)
REQUIRED_ONEWIRE_HIL_TOKENS = (
    "EV_HIL_ONEWIRE_PIN_MAP",
    "EV_HIL_ONEWIRE_TIMING",
    "EV_HIL_ONEWIRE_WIFI_TIMING",
)


def parse_pins(path: Path) -> dict[str, int]:
    pins: dict[str, int] = {}
    text = path.read_text(encoding="utf-8", errors="ignore")
    for match in PIN_RE.finditer(text):
        pins[match.group("name")] = int(match.group("gpio"))
    return pins


def check(root: Path = ROOT) -> list[str]:
    failures: list[str] = []
    pin_file = root / ATNEL_BOARD / "pins.def"
    profile = root / ATNEL_BOARD / "board_profile.h"
    i2c_hil = root / "adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil/main/ev_i2c_hil.c"
    onewire_hil = root / "adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil/main/ev_onewire_hil.c"

    if not pin_file.is_file():
        failures.append(f"missing ATNEL pin map: {pin_file.relative_to(root).as_posix()}")
    else:
        pins = parse_pins(pin_file)
        for name, expected_gpio in EXPECTED_ATNEL_PINS.items():
            if pins.get(name) != expected_gpio:
                failures.append(f"{pin_file.relative_to(root).as_posix()}: {name} expected_gpio={expected_gpio} actual_gpio={pins.get(name)}")

    if not profile.is_file():
        failures.append(f"missing ATNEL board profile: {profile.relative_to(root).as_posix()}")
    else:
        text = profile.read_text(encoding="utf-8", errors="ignore")
        for token in ("EV_BOARD_I2C_SCL_GPIO", "EV_BOARD_I2C_SDA_GPIO", "EV_BOARD_ONEWIRE_GPIO"):
            if token not in text:
                failures.append(f"{profile.relative_to(root).as_posix()}: missing {token}")
        for token in ("EV_BOARD_I2C_SPEED_SAFE_HZ", "EV_BOARD_I2C_FAST_400KHZ_REQUIRES_HIL"):
            if token not in text:
                failures.append(f"{profile.relative_to(root).as_posix()}: missing I2C speed policy token {token}")

    atb_pin_file = root / ATB_BOARD / "pins.def"
    atb_profile = root / ATB_BOARD / "board_profile.h"
    if not atb_pin_file.is_file():
        failures.append(f"missing ATB THERMO pin map: {atb_pin_file.relative_to(root).as_posix()}")
    else:
        pins = parse_pins(atb_pin_file)
        for name, expected_gpio in EXPECTED_ATB_PINS.items():
            if pins.get(name) != expected_gpio:
                failures.append(f"{atb_pin_file.relative_to(root).as_posix()}: {name} expected_gpio={expected_gpio} actual_gpio={pins.get(name)}")
    if not atb_profile.is_file():
        failures.append(f"missing ATB THERMO board profile: {atb_profile.relative_to(root).as_posix()}")
    else:
        text = atb_profile.read_text(encoding="utf-8", errors="ignore")
        for token in (
            "EV_BOARD_I2C_SCL_GPIO",
            "EV_BOARD_I2C_SDA_GPIO",
            "EV_BOARD_ONEWIRE_GPIO",
            "EV_BOARD_PWR_CTRL_GPIO",
            "EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW",
        ):
            if token not in text:
                failures.append(f"{atb_profile.relative_to(root).as_posix()}: missing {token}")

    for rel in REQUIRED_DOCS:
        doc = root / rel
        if not doc.is_file():
            failures.append(f"missing hardware-truth document: {rel}")

    if i2c_hil.is_file():
        text = i2c_hil.read_text(encoding="utf-8", errors="ignore")
        for token in REQUIRED_I2C_HIL_TOKENS:
            if token not in text:
                failures.append(f"{i2c_hil.relative_to(root).as_posix()}: missing {token}")
    else:
        failures.append(f"missing ATNEL I2C HIL source: {i2c_hil.relative_to(root).as_posix()}")

    if onewire_hil.is_file():
        text = onewire_hil.read_text(encoding="utf-8", errors="ignore")
        for token in REQUIRED_ONEWIRE_HIL_TOKENS:
            if token not in text:
                failures.append(f"{onewire_hil.relative_to(root).as_posix()}: missing {token}")
    else:
        failures.append(f"missing ATNEL OneWire HIL source: {onewire_hil.relative_to(root).as_posix()}")

    return failures


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        board = root / ATNEL_BOARD
        board.mkdir(parents=True)
        (board / "pins.def").write_text(
            'EV_BSP_PIN(PIN_I2C0_SCL, 4U, "SCL")\n'
            'EV_BSP_PIN(PIN_I2C0_SDA, 5U, "SDA")\n'
            'EV_BSP_PIN(PIN_ONEWIRE0_DQ, 12U, "DQ")\n',
            encoding="utf-8",
        )
        (board / "board_profile.h").write_text(
            "EV_BOARD_I2C_SCL_GPIO EV_BOARD_I2C_SDA_GPIO EV_BOARD_ONEWIRE_GPIO\n"
            "EV_BOARD_I2C_SPEED_SAFE_HZ EV_BOARD_I2C_FAST_400KHZ_REQUIRES_HIL\n",
            encoding="utf-8",
        )
        atb = root / ATB_BOARD
        atb.mkdir(parents=True)
        (atb / "pins.def").write_text("".join(f'EV_BSP_PIN({name}, {gpio}U, "ATB")\n' for name, gpio in EXPECTED_ATB_PINS.items()), encoding="utf-8")
        (atb / "board_profile.h").write_text(
            "EV_BOARD_I2C_SCL_GPIO EV_BOARD_I2C_SDA_GPIO EV_BOARD_ONEWIRE_GPIO EV_BOARD_PWR_CTRL_GPIO EV_ATB_THERMO_PWR_CTRL_ACTIVE_LOW\n",
            encoding="utf-8",
        )
        for rel in REQUIRED_DOCS:
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("doc\n", encoding="utf-8")
        i2c = root / "adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil/main/ev_i2c_hil.c"
        i2c.parent.mkdir(parents=True, exist_ok=True)
        i2c.write_text(" ".join(REQUIRED_I2C_HIL_TOKENS), encoding="utf-8")
        ow = root / "adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil/main/ev_onewire_hil.c"
        ow.parent.mkdir(parents=True, exist_ok=True)
        ow.write_text(" ".join(REQUIRED_ONEWIRE_HIL_TOKENS), encoding="utf-8")
        assert check(root) == []
        (board / "pins.def").write_text('EV_BSP_PIN(PIN_I2C0_SCL, 5U, "wrong")\n', encoding="utf-8")
        assert any("PIN_I2C0_SCL" in failure for failure in check(root))
    print("BOARD_WIRING_TRUTH_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check ATNEL board wiring truth and HIL marker contracts.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    failures = check(args.root.resolve())
    if failures:
        for failure in failures:
            print(f"BOARD_WIRING_TRUTH_FAILURE {failure}")
        print(f"board-wiring-truth-gate failed failures={len(failures)}")
        return 1
    print("board-wiring-truth-gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
