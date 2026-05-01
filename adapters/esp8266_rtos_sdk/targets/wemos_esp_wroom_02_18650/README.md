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
documentation identifies that as the practical default. 4 MB variants must be
selected explicitly by a documented local/variant configuration; the release
configuration does not assume them.

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
