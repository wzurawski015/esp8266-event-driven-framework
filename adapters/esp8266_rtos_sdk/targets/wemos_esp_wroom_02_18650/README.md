# Wemos ESP-WROOM-02 18650 target

This target has two honest firmware profiles:

- `boot_diag`: UART/LED diagnostic heartbeat retained for bring-up.
- `minimal_runtime`: runs the portable event runtime without claiming DS18B20,
  RTC, OLED, MCP23008, WDT or network hardware.

The board profile declares optional I2C pins but `EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK`
remains zero. A full hardware runtime profile must only be enabled after the
external wiring is documented and validated.

The minimal runtime exercises framework init, timers, quiescence, fault/metrics
storage and the cooperative scheduler through the same runtime app entrypoint
used by other ESP8266 targets.

## Release-validation target constraints

This target is classified as `physical_smoke` in `config/sdk_targets.def`.
The default SDK flash size is 2 MB / 16 Mbit because the supplied board
documentation identifies that as the practical default. Verified 4 MB modules are
supported only through the explicit `sdkconfig.flash_4mb.defaults` selector; the
release configuration does not assume them.

The target remains `minimal_runtime`. It does not claim DS18B20, RTC, OLED,
MCP23008, network or watchdog hardware without external wiring and HIL evidence.
Manual bootloader entry may be required: hold FLASH, press and release RESET,
then release FLASH before flashing.

## Minimal runtime smoke markers

The target emits two low-volume serial markers for release smoke validation:

```text
EV_WEMOS_SMOKE_BOOT board=wemos_esp_wroom_02_18650 profile=minimal_runtime
EV_WEMOS_SMOKE_RUNTIME_READY
```

A smoke PASS requires both markers from the physical board.

## Smoke monitor modes

`./tools/fw wemos-smoke-run` prefers the explicit firmware markers
`EV_WEMOS_SMOKE_BOOT` and `EV_WEMOS_SMOKE_RUNTIME_READY`. When the serial monitor
attaches after reset and misses those early lines, the monitor can accept a
runtime-alive fallback based on increasing `diag actor: tick=` and
`app actor: snapshot seq=` lines. The report identifies the PASS mode so marker
PASS and fallback PASS are auditable.

## Private-lab WiFi profile

The board profile itself keeps the safe fallback of `EV_BOARD_HAS_NET=0U` when no
secrets header is present. This private repository snapshot intentionally tracks
`bsp/wemos_esp_wroom_02_18650/board_secrets.local.h`, so Wemos WiFi is enabled
in a normal checkout and survives `git clean -fdx`. The target-local
`component.mk` detects that file and adds `-DEV_BOARD_INCLUDE_LOCAL_SECRETS=1`
only for this target. Do not use a global
`CFLAGS=-DEV_BOARD_INCLUDE_LOCAL_SECRETS=1`; it overwrites host-build include
flags and breaks portable tests.

Before publishing or sharing the repository, delete the tracked local secrets
header and remove the `.gitignore` exception for it.

Example build flow:

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_ESPPORT=/dev/ttyUSB1
./tools/fw sdk-defconfig
./tools/fw sdk-build
./tools/fw sdk-flash
./tools/fw sdk-monitor
```

For a verified 4 MB ESP-WROOM-02 module, select the flash variant explicitly and
regenerate `sdkconfig` before building:

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export FW_ESPPORT=/dev/ttyUSB1
export EV_WEMOS_FLASH_VARIANT=4mb
./tools/fw sdk-distclean
./tools/fw sdk-defconfig
./tools/fw sdk-build
./tools/fw sdk-flash
./tools/fw sdk-simple-monitor
```

`EV_WEMOS_FLASH_VARIANT=4mb` may also be used with `wemos-smoke-*` and
`wemos-wifi-*` helper commands. Leave the variable unset for release-matrix
validation so CI continues to exercise the conservative 2 MB default.

## Raw monitor Ctrl+C exit code 130

When using a raw monitor, quitting with Ctrl+C may make the terminal wrapper print `[process exited with code 130]`. This is `SIGINT` from the operator, not firmware failure. Release evidence tooling classifies it as `CONTROLLED_MONITOR_STOP` and still requires Wemos smoke markers or runtime-alive fallback evidence for PASS.
