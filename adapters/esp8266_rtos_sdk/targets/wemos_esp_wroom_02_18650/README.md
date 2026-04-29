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
