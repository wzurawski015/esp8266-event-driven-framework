# Stage 2 foundation quality gate

This document freezes the minimum acceptance bar for the current ESP8266 Stage 2 foundation.

The goal is to make the repository safe to extend with BSP and peripheral work without reopening basic operator or platform-contract questions.

## Green-state checklist

The foundation is considered green when all of the following are true:

- `./tools/fw host-test` passes,
- `./tools/fw docgen` passes and leaves generated artifacts in sync,
- `./tools/fw docs` passes without Doxygen warnings,
- `./tools/fw sdk-check` passes,
- `./tools/fw sdk-build` passes for `esp8266_generic_dev`,
- `./tools/fw sdk-clean-target` passes for `esp8266_generic_dev`,
- `./tools/fw sdk-distclean` passes for `esp8266_generic_dev`,
- `./tools/fw sdk-build` passes again for `esp8266_generic_dev` after `sdk-distclean`,
- `./tools/fw sdk-build` passes for `atnel_air_esp_motherboard`,
- `./tools/fw sdk-clean-target` passes for `atnel_air_esp_motherboard`,
- `./tools/fw sdk-distclean` passes for `atnel_air_esp_motherboard`,
- `./tools/fw sdk-build` passes again for `atnel_air_esp_motherboard` after `sdk-distclean`,
- `./tools/fw sdk-flash` can program the ATNEL target through Docker,
- `./tools/fw sdk-simple-monitor` shows readable runtime heartbeat logs at `115200`,
- `./tools/fw sdk-port-resolve` and `./tools/fw sdk-ports` reflect the same serial-resolution rules used by flash and monitor paths.

## Canonical operator truth

For the current ATNEL target, Docker remains the canonical operator path.

Preferred day-to-day sequence:

```bash
PORT="$(./tools/fw sdk-port-resolve)"

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-build

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard FW_ESPPORT="$PORT" ./tools/fw sdk-flash

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard FW_ESPPORT="$PORT" ./tools/fw sdk-simple-monitor
```

Use `sdk-monitor` only when you explicitly need the SDK-native interactive monitor behavior.

## Known limitations that are not framework regressions

The following behaviors are currently treated as environment constraints, not as framework defects:

- ESP8266 ROM boot text may appear at `74880` baud before the application switches to `115200`.
- Under Docker + WSL2, the first `sdk-flash` attempt may fail on DTR/RTS modem-control ioctls with `Errno 5`.
- In the current ESP8266 runtime path, target-side 64-bit `printf` format modifiers are not trusted for clean serial diagnostics.
- The current monotonic clock implementation preserves microsecond units but has an effective 1 ms resolution.
- The current monotonic source is backed by a 32-bit millisecond counter and therefore wraps after roughly 49.7 days.

As a result:

- runtime heartbeat logs intentionally print `mono_now_ms` as a 32-bit diagnostic view,
- `sdk-flash` includes retry and fallback guidance for transient serial-control failures and mirrors live flash output during each attempt,
- `sdk-flash-manual` exists as an explicit operator path that skips esptool-managed auto-reset before and after flashing,
- the generic and ATNEL targets stay on the same boot/diagnostic harness until BSP scope justifies a split.

## Manual-flash semantics

`./tools/fw sdk-flash-manual` is reserved for environments where Docker cannot reliably toggle DTR/RTS or where the auto-reset path reaches the serial port but not a clean ROM-loader sync state.

It assumes that the board is already placed into ROM bootloader mode manually.
It skips esptool-managed reset sequencing before and after flashing.
If it times out waiting for a packet header, the board did not reach ROM bootloader mode cleanly and the operator should retry the manual entry sequence.
After a successful manual flash, the operator should press **RESET** to boot the new firmware image.

## Target hygiene commands

The Stage 2 foundation also requires target-local cleanup symmetry.
Both of these commands are part of the supported operator surface:

```bash
./tools/fw sdk-clean-target
./tools/fw sdk-distclean
```

They must work for the default generic target and for board-scoped targets selected through `FW_SDK_PROJECT_DIR`.
`./tools/fw sdk-distclean` is wrapper-owned and must not depend on the presence of a vendor `make distclean` target.

## Expansion rule

Peripheral work may proceed only when the green-state checklist remains true.
If a later change breaks flashing, monitoring, build reproducibility, cleanup symmetry, or target-side diagnostics, that regression must be fixed before widening BSP scope.
