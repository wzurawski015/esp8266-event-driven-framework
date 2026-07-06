#!/usr/bin/env python3
"""Fail-closed ATB THERMO HIL log parser."""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

BOOT = re.compile(r"EV_ATB_THERMO_BOOT\s+target=atb_thermo_wemos_esp_wroom_02_4mb")
PIN_MAP = re.compile(r"EV_ATB_THERMO_PIN_MAP\s+scl=5\s+sda=4\s+onewire=13\s+pwr=0\s+led=2\s+pir_check=14\s+audio=12\s+pir_dis=15")
JP2 = re.compile(r"EV_ATB_THERMO_JP2_REQUIRED\s+position=2-3\s+mode=normal_pwr_ctrl")
PWR = re.compile(r"EV_ATB_THERMO_PWR_CTRL\s+gpio=0\s+active_low=1\s+state=ON\s+result=OK")
I2C = re.compile(r"EV_ATB_THERMO_I2C_READY\s+port=0\s+scl=5\s+sda=4\s+speed_hz=100000")
ONEWIRE = re.compile(r"EV_ATB_THERMO_ONEWIRE_READY\s+dq=13")
TEMP = re.compile(r"EV_DS18B20_TEMP\s+cC=-?\d+\s+C=-?\d+\.\d{2}")
LIGHT = re.compile(r"EV_BH1750_LIGHT\s+mLux=\d+\s+lux=\d+\.\d{3}")
RESULT = re.compile(r"EV_HIL_ATB_THERMO_RESULT\s+PASS\s+failures=0")

REQUIRED = (
    ("boot", BOOT),
    ("pin_map", PIN_MAP),
    ("jp2", JP2),
    ("pwr_ctrl", PWR),
    ("i2c_ready", I2C),
    ("onewire_ready", ONEWIRE),
    ("temperature", TEMP),
    ("light", LIGHT),
    ("result", RESULT),
)


def evaluate(text: str) -> tuple[str, list[str]]:
    missing = [name for name, regex in REQUIRED if regex.search(text) is None]
    return ("PASS" if not missing else "FAIL", missing)


def self_test() -> None:
    good = "\n".join([
        "EV_ATB_THERMO_BOOT target=atb_thermo_wemos_esp_wroom_02_4mb board=x flash=4mb",
        "EV_ATB_THERMO_PIN_MAP scl=5 sda=4 onewire=13 pwr=0 led=2 pir_check=14 audio=12 pir_dis=15",
        "EV_ATB_THERMO_JP2_REQUIRED position=2-3 mode=normal_pwr_ctrl",
        "EV_ATB_THERMO_PWR_CTRL gpio=0 active_low=1 state=ON result=OK",
        "EV_ATB_THERMO_I2C_READY port=0 scl=5 sda=4 speed_hz=100000",
        "EV_ATB_THERMO_ONEWIRE_READY dq=13",
        "EV_DS18B20_TEMP cC=2345 C=23.45",
        "EV_BH1750_LIGHT mLux=12345 lux=12.345",
        "EV_HIL_ATB_THERMO_RESULT PASS failures=0",
    ])
    assert evaluate(good)[0] == "PASS"
    assert evaluate(good.replace("EV_HIL_ATB_THERMO_RESULT PASS failures=0", ""))[0] == "FAIL"
    print("ATB_THERMO_HIL_PARSER_SELF_TEST PASS")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Parse ATB THERMO HIL evidence log")
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        self_test()
        return 0
    if args.log is None:
        print("hil-atb-thermo-gate ENVIRONMENT_BLOCKED: set EV_ATB_THERMO_HIL_LOG or pass a log path")
        return 77
    try:
        text = args.log.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        print(f"hil-atb-thermo-gate ENVIRONMENT_BLOCKED: cannot read log: {exc}")
        return 77
    status, missing = evaluate(text)
    if status == "PASS":
        print("hil-atb-thermo-gate passed status=REAL_HIL_PASS")
        return 0
    print(f"hil-atb-thermo-gate failed missing={','.join(missing)}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
