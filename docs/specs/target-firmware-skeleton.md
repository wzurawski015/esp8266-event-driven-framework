# Target firmware skeleton

Stage 2A2 introduces the first SDK-native ESP8266 RTOS SDK project skeleton.

Current target path:

```text
adapters/esp8266_rtos_sdk/targets/esp8266_generic_dev/
```

This target is intentionally minimal and now acts as the golden-reference target for ESP8266 target-side validation.

Goals of this stage:

- prove that the pinned SDK image can build a real target project,
- establish the canonical Docker operator workflow around `defconfig`, `build`, cleanup symmetry, flash, and monitor,
- keep one board-neutral target permanently green in CI,
- keep the generic target on the same framework-backed bring-up path as the first board target,
- provide a stable place where later board-specific bring-up can land.

## Project structure

```text
adapters/esp8266_rtos_sdk/targets/esp8266_generic_dev/
├── Makefile
├── README.md
├── sdkconfig.defaults
└── main/
    ├── app_main.c
    └── component.mk
```

## Why this project is intentionally small

The first target project is not yet the full framework firmware.

It is a controlled bring-up target used to verify:

- SDK image reproducibility,
- project layout correctness,
- `sdkconfig.defaults` and `defconfig` flow,
- compile/flash/monitor wiring through `./tools/fw`,
- wrapper-owned cleanup symmetry,
- board-scoped build outputs and generated `sdkconfig`,
- reuse of the shared ESP8266 boot/diagnostic harness.

## Canonical commands

```bash
PORT="$(./tools/fw sdk-port-resolve)"

./tools/fw sdk-check
./tools/fw sdk-defconfig
./tools/fw sdk-build
./tools/fw sdk-clean-target
./tools/fw sdk-distclean
./tools/fw sdk-build

FW_ESPPORT="$PORT" ./tools/fw sdk-flash
FW_ESPPORT="$PORT" ./tools/fw sdk-flash-manual
FW_ESPPORT="$PORT" FW_MONITOR_BAUD=115200 ./tools/fw sdk-simple-monitor
```

`sdk-defconfig` uses `sdkconfig.defaults` as the project default configuration seed.
`sdk-build` will auto-materialize `sdkconfig` via `defconfig` if it is missing.

## Current firmware behavior

The current `app_main()` is intentionally small but no longer bypasses the public platform ports.
It delegates to the shared ESP8266 boot/diagnostic helper and proves:

- board profile reporting,
- reset-reason reporting,
- public clock/log/reset/uart port integration,
- periodic monotonic heartbeat logs,
- target-local serial runtime viability.

This is deliberate.
Framework-backed actors and wider adapter wiring will be layered in later Stage 2 steps.

## Finalization policy for `esp8266_generic_dev`

`esp8266_generic_dev` is not the final board support package.

It is the smallest target that must remain:

- reproducible,
- board-neutral,
- CI-buildable without attached hardware,
- stable enough to separate framework/toolchain regressions from board-specific wiring issues,
- close enough to the first board target that operator and adapter regressions are detected early.

Board-specific peripherals such as OLED, RTC, MCP23008, 1-Wire sensor networks, IR receivers, or WS2812-driven helper hardware belong in dedicated BSP profiles rather than this generic target.
