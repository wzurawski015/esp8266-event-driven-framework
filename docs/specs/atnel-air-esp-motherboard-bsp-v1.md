# ATNEL AIR ESP motherboard BSP v1

Stage 2A3 freezes the first board-specific BSP profile for the following stack:

- ATNEL AIR ESP module,
- ATB WIFI ESP Motherboard,
- ESP8266 target path through the FT231X USB-UART interface.

## Scope of BSP v1

This stage does **not** yet claim full board integration.

A Stage 2A4 companion target now provides the first board-scoped boot/diag bring-up
using this BSP, but only through clock/log/reset/uart.

It now freezes:

- the primary GPIO ownership map,
- the runtime hardware graph consumed by `ev_demo_app_board_profile_t`,
- the I2C addresses for RTC, MCP23008, and OLED,
- the supervisor required/optional device masks,
- the safe jumper baseline for early framework bring-up,
- the enablement order for later adapter work.

## Primary board map

| Logical function | GPIO / path | Notes |
| --- | --- | --- |
| UART0 TX | GPIO1 | via FT231X |
| UART0 RX | GPIO3 | via FT231X |
| I2C SCL | GPIO4 | shared bus |
| I2C SDA | GPIO5 | shared bus |
| 1-Wire DQ | GPIO12 | via JP1 |
| Optional IR input | GPIO13 | via JP4 |
| Optional interrupt line | GPIO14 | MCP23008 INT via JP2 or RTC INT via JP19 |
| Bootstrap / optional Magic SCK | GPIO15 | bootstrap-sensitive |
| Optional Magic DIN | GPIO16 | helper path |


## Runtime board profile policy

`bsp/atnel_air_esp_motherboard/board_profile.h` is the single source of truth
for the runtime actor graph on this board. The target composition root converts
that header into `ev_demo_app_board_profile_t`; the portable app must not fall
back to actor default I2C addresses for RTC, MCP23008, or OLED.

| Runtime item | BSP macro | Value / policy |
| --- | --- | --- |
| RTC address | `EV_BOARD_RTC_ADDR_7BIT` | `0x68` |
| MCP23008 address | `EV_BOARD_MCP23008_ADDR_7BIT` | `0x20` |
| OLED address | `EV_BOARD_OLED_ADDR_7BIT` | `0x3C` |
| OLED controller | `EV_BOARD_OLED_CONTROLLER` | board-owned controller enum |
| RTC SQW line | `EV_BOARD_RTC_SQW_LINE_ID` | logical IRQ line id |
| Present hardware | `EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK` | actors that may be bound |
| Required readiness | `EV_BOARD_SUPERVISOR_REQUIRED_MASK` | devices needed for `EV_SYSTEM_READY` |
| Optional readiness | `EV_BOARD_SUPERVISOR_OPTIONAL_MASK` | degraded-mode devices |

A board that does not declare a device in `EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK`
must not have that hardware actor bound into the runtime graph. Static routes may
exist in `config/routes.def`, but runtime delivery masks disabled actors and
counts those deliveries instead of treating absent board hardware as a registry
failure.

## Safe jumper baseline

Recommended early baseline:

- keep JP2 open,
- keep JP4 open,
- keep JP14 open,
- keep JP16 open,
- keep JP19 open,
- keep JP1 open until the dedicated 1-Wire stage.

This isolates boot, diagnostics, and early adapter work from optional board-side couplings.

## Why the board is not treated as generic

This board already couples multiple peripherals to the same ESP8266 pins:

- the module OLED and motherboard devices share the I2C bus,
- the motherboard offers DS18B20 headers on the same 1-Wire line,
- GPIO13 / GPIO14 / GPIO15 / GPIO16 may be repurposed by optional peripherals through jumpers.

Because of that, board-specific policy belongs in this BSP rather than in
`esp8266_generic_dev`.

## Planned enablement order after BSP v1

1. UART boot + diagnostics
2. clock / log / reset / uart public adapters
3. board-scoped boot and diagnostics target
4. I2C bus bring-up
5. OLED + RTC
6. MCP23008
7. 1-Wire / DS18B20
8. IR and Magic Hercules helpers
