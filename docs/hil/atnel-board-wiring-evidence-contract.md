# ATNEL board wiring evidence contract

This project does not derive I2C or 1-Wire pins from generic ESP8266 examples.
The active board profile is the single source of truth for firmware, and real
hardware acceptance still requires either continuity testing or a logic analyzer.

## ATNEL AIR ESP module

The ATNEL AIR ESP schematic/netlist establishes these named nets:

| Net | Schematic / netlist connection | Firmware logical pin |
|---|---|---|
| `SCL` | `J1-3`, `J2-3`, `R6-1`, `U1-13` | `PIN_I2C0_SCL` |
| `SDA` | `J1-4`, `J2-2`, `R5-1`, `U1-14` | `PIN_I2C0_SDA` |
| `1-WIRE` | `J2-1`, `R4-1`, `U1-6` | `PIN_ONEWIRE0_DQ` |

For the ESP-12S footprint used by this BSP, the board profile maps those nets to
`GPIO4` for SCL, `GPIO5` for SDA, and `GPIO12` for 1-Wire DQ.  Wemos-class
profiles may legitimately use a different SCL/SDA order, so do not introduce a
global `board_config.h` or hard-code an `i2c_init(0, 5, 4)` style assumption.

## ATNEL WIFI ESP MOTHERBOARD

The motherboard connector identifies `(SCL)GP04`, `(SDA)GP05`, and `(DQ)GP12`.
SCL/SDA are a shared motherboard bus, not a private OLED-only link: the bus also
reaches `I2C OUT`, RTC `DS1337`, MCP23008, and any external modules attached to
the connector.  DQ reaches the DS18B20 headers and the 1-Wire jumper path.

## Firmware evidence marker

Real ATNEL I2C and OneWire HIL transcripts must print the active BSP mapping:

```text
EV_HIL_BOARD_PIN_MAP board=<name> i2c_port=0 scl_gpio=<n> sda_gpio=<n> onewire_gpio=<n> source=bsp/atnel_air_esp_motherboard/pins.def
EV_HIL_ONEWIRE_PIN_MAP board=<name> dq_gpio=<n> pullup=external required=1 source=bsp/atnel_air_esp_motherboard/pins.def wifi=<on|off>
```

A parser self-test proves only parser behavior.  Physical wiring is accepted only
when the transcript also contains real bus activity and, for timing-sensitive
OneWire truth, a WiFi-on timing run or an explicit `ENVIRONMENT_BLOCKED` result.
