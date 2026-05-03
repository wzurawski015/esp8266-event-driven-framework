#!/usr/bin/env python3
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
errors: list[str] = []

PROFILE_REQUIRED = [
    "EV_TARGET_NAME",
    "EV_TARGET_USB_UART_BRIDGE",
    "EV_TARGET_FLASH_BAUD",
    "EV_TARGET_MONITOR_BAUD",
]

def profile_has(profile: Path, key: str) -> bool:
    text = profile.read_text(encoding="utf-8", errors="ignore")
    return re.search(rf"^\s*{re.escape(key)}=", text, re.M) is not None

for target_dir in sorted((ROOT / "adapters" / "esp8266_rtos_sdk" / "targets").iterdir()):
    if not target_dir.is_dir():
        continue
    profile = target_dir / "target_usb_uart.profile"
    if not profile.exists():
        errors.append(f"missing target_usb_uart.profile in {target_dir.name}")
    else:
        for key in PROFILE_REQUIRED:
            if not profile_has(profile, key):
                errors.append(f"{target_dir.name}: USB profile missing {key}")
    sdkconfig = target_dir / "sdkconfig.defaults"
    makefile = target_dir / "Makefile"
    main_dir = target_dir / "main"
    if makefile.exists() or main_dir.exists():
        if not sdkconfig.exists():
            errors.append(f"{target_dir.name}: buildable target missing sdkconfig.defaults")
        elif not sdkconfig.read_text(encoding="utf-8", errors="ignore").strip():
            errors.append(f"{target_dir.name}: sdkconfig.defaults is empty")
    if target_dir.name == "wemos_esp_wroom_02_18650":
        text = sdkconfig.read_text(encoding="utf-8", errors="ignore") if sdkconfig.exists() else ""
        if "CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y" not in text and "EV_WEMOS_FLASH_VARIANT" not in text:
            errors.append("wemos_esp_wroom_02_18650: default flash size must be 2MB or an explicit variant selector")
        board = (ROOT / "bsp" / "wemos_esp_wroom_02_18650" / "board_profile.h").read_text(encoding="utf-8", errors="ignore")
        if "EV_BOARD_RUNTIME_PROFILE_FULL 0U" not in board:
            errors.append("wemos_esp_wroom_02_18650: board profile must not claim full runtime by default")
        if "EV_BOARD_RUNTIME_PROFILE_MINIMAL 1U" not in board:
            errors.append("wemos_esp_wroom_02_18650: minimal runtime profile must remain enabled by default")
        if "#ifndef EV_BOARD_HAS_NET" not in board or "#define EV_BOARD_HAS_NET 0U" not in board:
            errors.append("wemos_esp_wroom_02_18650: network capability must be opt-in, defaulting to EV_BOARD_HAS_NET 0U")
        if "EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK 0U" not in board:
            errors.append("wemos_esp_wroom_02_18650: hardware-present mask must remain zero without external wiring validation")
        required_net_defaults = [
            "EV_BOARD_NET_WIFI_SSID",
            "EV_BOARD_NET_WIFI_PASSWORD",
            "EV_BOARD_NET_WIFI_AUTH_MODE",
            "EV_BOARD_NET_MQTT_BROKER_URI",
            "EV_BOARD_NET_MQTT_CLIENT_ID",
            "EV_BOARD_NET_COMMAND_TOKEN",
            "EV_BOARD_REMOTE_COMMAND_CAPABILITIES",
        ]
        for macro in required_net_defaults:
            if macro not in board:
                errors.append(f"wemos_esp_wroom_02_18650: board profile missing safe default for {macro}")
        component_mk = target_dir / "main" / "component.mk"
        component_text = component_mk.read_text(encoding="utf-8", errors="ignore") if component_mk.exists() else ""
        if "board_secrets.local.h" not in component_text or "EV_BOARD_INCLUDE_LOCAL_SECRETS" not in component_text:
            errors.append("wemos_esp_wroom_02_18650: component.mk must provide target-local board_secrets.local.h opt-in")
        if "CFLAGS += -DEV_BOARD_INCLUDE_LOCAL_SECRETS=1" not in component_text:
            errors.append("wemos_esp_wroom_02_18650: local secrets opt-in must not rely on global CFLAGS")
        example = ROOT / "bsp" / "wemos_esp_wroom_02_18650" / "board_secrets.example.h"
        if not example.exists():
            errors.append("wemos_esp_wroom_02_18650: missing sanitized board_secrets.example.h")
        else:
            example_text = example.read_text(encoding="utf-8", errors="ignore")
            if "your-2g4-ssid" not in example_text or "your-password" not in example_text:
                errors.append("wemos_esp_wroom_02_18650: example secrets must contain placeholders, not real credentials")

tracked_local = []
try:
    import subprocess
    completed = subprocess.run(["git", "-C", str(ROOT), "ls-files", "adapters/esp8266_rtos_sdk/targets/*/target_usb_uart.local.profile"], stdout=subprocess.PIPE, text=True, check=False)
    tracked_local = [line for line in completed.stdout.splitlines() if line.strip()]
except Exception:
    tracked_local = []
if tracked_local:
    errors.append("local USB-UART override profile is tracked: " + ", ".join(tracked_local))

i2c_hil_c = ROOT / "adapters" / "esp8266_rtos_sdk" / "targets" / "atnel_air_esp_motherboard_i2c_hil" / "main" / "ev_i2c_hil.c"
if i2c_hil_c.exists():
    i2c_text = i2c_hil_c.read_text(encoding="utf-8", errors="ignore")
    if '#include "board_profile.h"' not in i2c_text:
        errors.append("atnel_air_esp_motherboard_i2c_hil: ev_i2c_hil.c must include board_profile.h for SDA/SCL diagnostics")
    if "ATNEL I2C HIL requires EV_BOARD_I2C_SDA_GPIO" not in i2c_text:
        errors.append("atnel_air_esp_motherboard_i2c_hil: missing hard guard for SDA/SCL diagnostic GPIOs")
else:
    errors.append("atnel_air_esp_motherboard_i2c_hil: missing ev_i2c_hil.c")


if errors:
    for error in errors:
        print(f"error: {error}")
    print(f"EV_SDK_TARGET_DEFAULTS_CHECK FAIL failures={len(errors)}")
    sys.exit(1)
print("EV_SDK_TARGET_DEFAULTS_CHECK PASS")
