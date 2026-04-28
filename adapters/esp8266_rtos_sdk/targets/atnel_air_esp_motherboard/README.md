# atnel_air_esp_motherboard target

This target is the first board-scoped firmware image for the ATNEL AIR ESP module
mounted on the ATB WIFI ESP Motherboard.

Current scope:

- boot and diagnostics over UART0 / FT231X,
- validation of the first concrete ESP8266 RTOS SDK port adapters,
- validation of the first real event-driven runtime on hardware,
- board identification and reset-reason reporting,
- cooperative system-pump execution across APP and DIAG actors,
- monotonic-time tick publication through the public clock port.

Canonical commands from the repository root:

```bash
PORT="$(./tools/fw sdk-port-resolve)"
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-defconfig
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-build
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-clean-target
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-distclean
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-build

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard FW_ESPPORT="$PORT" ./tools/fw sdk-flash
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard FW_ESPPORT="$PORT" ./tools/fw sdk-flash-manual
FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard FW_ESPPORT="$PORT" ./tools/fw sdk-simple-monitor
```

Recommended early jumper baseline:

- JP2 open
- JP4 open
- JP14 open
- JP16 open
- JP19 open
- JP1 open

`tools/fw` now selects the monitor baud from target context by default. For this target, the runtime default is `115200`.
If `sdk-monitor` is unstable under Docker or WSL2, use `sdk-simple-monitor` as the canonical fallback.
If `sdk-flash` fails with a DTR/RTS I/O error, handshake timeout, or `Invalid head of packet (...)` under Docker or WSL2, use `sdk-flash-manual` after putting the board into ROM bootloader mode manually. That path now skips esptool-managed pre-flash auto-reset and post-flash reset, so press **RESET** once flashing finishes. If manual flash times out, treat it as a board-state problem: the target never entered ROM bootloader mode cleanly.
If you need the log from the first application line, start `sdk-simple-monitor` first and then press **RESET** on the board.
The frozen operator acceptance bar for this target lives in `docs/specs/stage2-foundation-quality-gate.md`.

## USB-UART profile

Host-side USB-UART metadata lives in `target_usb_uart.profile`. The ATNEL AIR ESP
Motherboard profile uses the FT231X / FTDI-class bridge hint. `FW_ESPPORT` always
wins over auto-detection, and `target_usb_uart.local.profile` may be used for
fixture-specific serial numbers.
