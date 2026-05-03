# Platform contracts

This document freezes the Stage 2 platform boundary for ESP8266 integration.

## Objectives

The platform-agnostic parts of the framework must remain unaware of:

- ESP8266 RTOS SDK headers,
- FreeRTOS task/queue/timer handles,
- board-specific GPIO assignments,
- UART driver details,
- reset/watchdog implementation specifics,
- concrete wall-clock sources.

These details cross the boundary only through `ports/include/ev/*.h` and are
implemented concretely in `adapters/`.

## Boundary rules

1. `ports/include/ev/*.h` defines *what* the platform must provide.
2. `adapters/` defines *how* ESP8266 RTOS SDK satisfies those contracts.
3. `bsp/` owns board-level pin maps and board policy.
4. `core/` and `domain/` must never include ESP8266 RTOS SDK headers.
5. Port contracts use standard C types plus framework-owned enums/result codes.
6. Port contracts must keep ownership and ISR legality explicit.

## Stage 2A1 initial platform ports

The initial contract set is intentionally small:

- `port_clock.h`
- `port_log.h`
- `port_reset.h`
- `port_gpio.h`
- `port_uart.h`

This is enough to freeze the boundary needed for:

- boot diagnostics,
- time and timestamps,
- reset reason + restart,
- UART console/logging,
- first GPIO-based smoke tests.

## Contract style

Every public port contract should:

- expose a small C struct of function pointers,
- carry adapter state via `void *ctx`,
- return `ev_result_t` for fallible operations,
- avoid SDK-native types in public signatures,
- avoid hidden allocation policy,
- be implementable by host-side fakes.

## BSP policy

Board support starts with explicit per-board directories under `bsp/`.

Board-level pin numbers must not leak into `core/`, `domain/`, or `ports/`.
Logical-to-physical mappings belong in board-owned files such as `pins.def`.

## What Stage 2A1 does not do yet

Stage 2A1 does not yet:

- implement concrete ESP8266 adapters,
- define a production board,
- integrate Wi-Fi, NVS, or I2C drivers,
- build deployable firmware.

It only freezes the target-side toolchain contract and the public platform
boundary.


## Log pending hook

`ev_log_port_t.pending` is a non-blocking optional hook for buffered-log visibility. A NULL hook means zero observable pending records.

## Adapter bootstrap exceptions

Adapter/bootstrap SDK primitives are not allowed implicitly. `make static-contracts` validates `tools/audit/adapter_exception_allowlist.def`, rejects unapproved occurrences, and treats stale allowlist rows as errors. `xTaskCreate` is allowed only as a HIL bootstrap fallback; `xTaskCreateStatic` is classified as static-safe.
