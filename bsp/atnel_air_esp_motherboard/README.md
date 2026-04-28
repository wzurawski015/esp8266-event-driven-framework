# atnel_air_esp_motherboard

This is the first concrete board support package profile for the hardware stack:

- ATNEL AIR ESP module (ESP-12X / ESP-07S style path selected),
- mounted on ATB WIFI ESP Motherboard.

Stage 2A3 only freezes the board profile and safe jumper baseline.
It does not yet claim that all board peripherals are wired through public
framework adapters.

## Board facts captured in this BSP

- UART0 is provided through the onboard FT231X USB-UART path.
- Primary I2C uses `GPIO4` (SCL) and `GPIO5` (SDA).
- Primary 1-Wire uses `GPIO12` through JP1.
- `GPIO13` may be connected to the IR receiver path through JP4.
- `GPIO14` may be used either as MCP23008 interrupt (JP2) or RTC interrupt (JP19).
- `GPIO15` and `GPIO16` participate in the Magic Hercules / WS2812 helper path through jumpers.

## Safe bring-up baseline

For early framework bring-up, keep optional board couplings disabled unless a
specific stage is validating them.

Recommended baseline:

- JP2 open
- JP4 open
- JP14 open
- JP16 open
- JP19 open
- JP1 open until the dedicated 1-Wire bring-up stage

This keeps generic UART boot/diag work separated from optional board-side side
channels and interrupt wiring.

## Planned enablement order

1. boot + diagnostics over UART0
2. clock / reset / log / uart public adapters
3. I2C bus bring-up
4. OLED and RTC probe
5. MCP23008 interrupt-driven path
6. 1-Wire / DS18B20
7. IR receiver and Magic Hercules / WS2812 helpers


Stage 2A4 also adds the first board-scoped target project under
`adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard/`, which uses the
public clock/log/reset/uart ports for boot diagnostics.
