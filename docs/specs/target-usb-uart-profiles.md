# Target USB-UART profiles

Status: target-host integration specification.

USB-UART bridge metadata is host-side target integration data. It describes what
the developer workstation sees while flashing or monitoring a board. It is not a
runtime ESP8266 capability, and it must not be included by `core/`, `ports/`,
actors, or board runtime BSP capability masks.

## Profile location

Each SDK target may provide:

```text
adapters/esp8266_rtos_sdk/targets/<target>/target_usb_uart.profile
```

Developer-local overrides may be placed next to it as:

```text
adapters/esp8266_rtos_sdk/targets/<target>/target_usb_uart.local.profile
```

The local override is ignored by Git and may contain fixture-specific serial
numbers or `/dev/serial/by-id` patterns.

## Resolution order

`tools/fw sdk-port-resolve`, `sdk-flash`, `sdk-monitor`, and HIL flash/monitor
commands resolve ports in this order:

1. `FW_ESPPORT` explicit override.
2. `ESPPORT` compatibility override.
3. `EV_TARGET_PORT_HINT` from the target profile/local profile.
4. `/dev/serial/by-id` match by `EV_TARGET_USB_SERIAL`.
5. `/dev/serial/by-id` match by `EV_TARGET_SERIAL_BY_ID_PATTERN`.
6. `udevadm` VID/PID match using `EV_TARGET_USB_VID` and `EV_TARGET_USB_PID`.
7. Safe fallback only when exactly one `/dev/ttyUSB*` or `/dev/ttyACM*` node is visible.

If multiple devices match, the tool fails. This is intentional; flashing the
wrong board is worse than requiring an explicit override.

## Current profiles

| Target | Bridge metadata | VID:PID policy |
|---|---|---|
| `atnel_air_esp_motherboard` | FT231X / FTDI-class | common FT231X hint `0403:6015` |
| `atnel_air_esp_motherboard_i2c_hil` | FT231X / FTDI-class | common FT231X hint `0403:6015` |
| `atnel_air_esp_motherboard_onewire_hil` | FT231X / FTDI-class | common FT231X hint `0403:6015` |
| `atnel_air_esp_motherboard_wifi_hil` | FT231X / FTDI-class | common FT231X hint `0403:6015` |
| `wemos_esp_wroom_02_18650` | CP2102 / CP210x | common CP210x hint `10c4:ea60` |
| `wemos_d1_mini` / ZY8266 | CH340 / CH341 | common WCH hint `1a86:7523` |
| `esp8266_generic_dev` | unknown / user-provided | no VID/PID by default |
| `adafruit_feather_huzzah_esp8266` | CP2104 / CP210x common profile placeholder | common CP210x hint; verify per board |

The VID/PID values are matching hints, not immutable board guarantees. Board
variants and clone adapters may differ. Use a local profile or `FW_ESPPORT` when
in doubt.

## Local override example

```sh
cat > adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard/target_usb_uart.local.profile <<'PROFILE'
EV_TARGET_USB_SERIAL="FTDI_SERIAL_FROM_BY_ID"
EV_TARGET_SERIAL_BY_ID_PATTERN="*/usb-FTDI_FT231X_USB_UART_FTDI_SERIAL_FROM_BY_ID*"
PROFILE
```

or simply:

```sh
FW_ESPPORT=/dev/serial/by-id/usb-FTDI_FT231X_USB_UART_... ./tools/fw sdk-flash
```

## Validation

Run:

```sh
./tools/fw target-usb-profile-check
./tools/fw sdk-port-resolve-test
```

These checks do not require hardware. Physical detection still requires the
actual board and adapter to be connected.
