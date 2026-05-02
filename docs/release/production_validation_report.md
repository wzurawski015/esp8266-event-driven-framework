# Production validation report

Baseline archive: `esp8266-event-driven_20260430_160807.tar.gz`.

This report starts the production-validation iteration after no-legacy framework
hardening. It intentionally separates host/docs validation from SDK build,
linker-map memory, HIL and Wemos physical-board smoke validation.

## Confirmed no-legacy baseline

Inspected files and symbols:

- `apps/demo/include/ev/demo_app.h`: `ev_demo_app_t` owns `ev_runtime_graph_t graph`,
  timer tokens and application actor contexts. It does not own `ev_mailbox_t`,
  per-actor mailbox storage arrays, `ev_actor_runtime_t`, `ev_actor_registry_t`,
  `ev_domain_pump_t`, `ev_system_pump_t`, `next_tick_ms` or `next_tick_100ms_ms`.
- `apps/demo/ev_demo_app.c`: demo polling delegates through runtime-loop APIs and
  does not call `ev_actor_registry_bind()`, `ev_domain_pump_init()`,
  `ev_system_pump_init()` or `ev_system_pump_run()` as a composition root.
- `runtime/include/ev/runtime_loop.h` and `runtime/src/ev_runtime_loop.c`: reusable
  runtime-loop layer is present outside `apps/demo`.
- `runtime/include/ev/actor_publish_port.h` and `runtime/src/ev_actor_publish_port.c`:
  graph-backed actor publish port is present and tested.
- `ports/include/ev/port_log.h` and `runtime/src/ev_quiescence_service.c`: optional
  log `pending` hook exists and `pending_log_records` is policy-controlled.
- `tools/audit/static_contracts.py`: no-legacy contracts hard-fail legacy demo
  ownership and direct graph-internal access from demo code.

## Observed user-provided validation log

The uploaded `walidacja_esp8266.txt` contains host-side validation lines for:

| Gate | Observed status from uploaded log | Evidence marker |
|---|---:|---|
| `make routegen-check` | PASS | `routegen-check passed: 53 routes` |
| `make static-contracts` | PASS | `static contracts passed` |
| `make memory-budget` | PASS | `memory budget passed` |
| `make host-test` | PASS | `host tests passed` |
| `make property-test` | PASS | `property tests passed` |
| `make quality-gate` | PASS | `quality-gate passed` |
| `make release-gate` | PASS | `release-gate passed` |

The same log does not contain HIL PASS markers for ATNEL I2C, OneWire, WiFi or
Wemos smoke. Those areas remain `NOT_RUN` unless validated separately with serial
logs and the required monitor markers.

## Validation status model

Every production validation row must use exactly one of:

- `PASS`
- `FAIL`
- `NOT_RUN`
- `ENVIRONMENT_BLOCKED`
- `NOT_APPLICABLE`

A host PASS does not imply SDK, linker-map memory, HIL or Wemos smoke PASS.

## Target inventory baseline

| Target | Path | Class | Reason |
|---|---|---|---|
| `esp8266_generic_dev` | `adapters/esp8266_rtos_sdk/targets/esp8266_generic_dev` | `buildable_sdk` | Has Makefile, `main/`, `sdkconfig.defaults` and USB-UART profile. |
| `atnel_air_esp_motherboard` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard` | `buildable_sdk` | Main ATNEL runtime target. |
| `atnel_air_esp_motherboard_i2c_hil` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | Buildable SDK project for I2C HIL. |
| `atnel_air_esp_motherboard_onewire_hil` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | Buildable SDK project for OneWire HIL. |
| `atnel_air_esp_motherboard_wifi_hil` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | Buildable SDK project for WiFi HIL. |
| `wemos_d1_mini` | `adapters/esp8266_rtos_sdk/targets/wemos_d1_mini` | `buildable_sdk` | Buildable Wemos D1 Mini target. |
| `wemos_esp_wroom_02_18650` | `adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650` | `physical_smoke` | Buildable minimal-runtime target, physical smoke requires board. |
| `adafruit_feather_huzzah_esp8266` | `adapters/esp8266_rtos_sdk/targets/adafruit_feather_huzzah_esp8266` | `metadata_only` | Has USB-UART metadata only; no SDK Makefile/main project in this snapshot. |

## Wemos ESP-WROOM-02 18650 evidence

From the supplied Wemos documentation: the board uses ESP8266EX / ESP-WROOM-02,
CP2102 USB-UART, microUSB, RESET and FLASH buttons, a GPIO16/D0 blue user LED,
a sleep terminal for GPIO16-to-RST deep-sleep wake, A0 through a divider, and a
practical default flash size of 2 MB / 16 Mbit while 4 MB variants may exist.
The firmware target must remain minimal-runtime unless external sensors/wiring
are explicitly documented and validated.

## Current production-validation status

| Area | Status | Notes |
|---|---:|---|
| Host quality gate | PASS | Observed in uploaded validation log. |
| Docs/release gate | PASS | Observed in uploaded validation log. |
| SDK toolchain check | NOT_RUN | Toolchain log is not stored in this archive snapshot. |
| SDK build matrix | PASS | `docs/release/sdk_build_matrix_report.md` contains committed PASS rows for buildable, HIL SDK and Wemos physical-smoke targets. |
| SDK linker-map memory matrix | NOT_RUN | `docs/release/sdk_memory_matrix_report.md` contains no committed EV_MEM rows in this archive snapshot. |
| ATNEL I2C HIL | FAIL | Isolated to `sda-stuck-low-containment`; base I2C cases passed. |
| ATNEL OneWire HIL | NOT_RUN | Requires physical fixture and serial PASS marker. |
| ATNEL WiFi HIL | NOT_RUN | Requires physical fixture and serial PASS marker. |
| Wemos minimal runtime smoke | NOT_RUN | Requires physical Wemos board and serial smoke markers. |

## Scope of this iteration

This iteration adds target matrix tooling, SDK matrix reports, CI SDK matrix
workflow, Wemos target constraints, linker-map memory budget gates, HIL release
runners, Wemos smoke tooling and a final consolidated release status model.

## Commit 2 SDK matrix tooling

`config/sdk_targets.def` is now the SSOT for SDK target classification.
`tools/sdk_matrix.py` and `tools/audit/sdk_matrix_check.py` validate that every
SDK target directory is listed exactly once and that buildable targets have the
required SDK project files. `tools/fw` exposes `sdk-target-list`,
`sdk-matrix-check`, `sdk-build-one`, `sdk-build-matrix` and
`sdk-build-matrix-report`.

## CI policy update

SDK build matrix validation is defined in `.github/workflows/sdk-matrix.yml`.
Hardware HIL is defined in `.github/workflows/hil-self-hosted.yml` and requires
manual `workflow_dispatch`, a self-hosted `esp8266-hil` runner, a serial port and
explicit hardware confirmation. GitHub-hosted CI must not report HIL PASS.

## Commit 4 target-defaults normalization

Wemos ESP-WROOM-02 18650 is constrained to 2 MB default flash and minimal-runtime
status unless a documented hardware variant is introduced. The target-defaults
audit checks USB-UART profiles, buildable target defaults and Wemos no-false-full-runtime constraints.

## Commit 5 SDK linker-map memory matrix

`config/sdk_memory_budgets.def` and `tools/sdk_memory_matrix.py` now define the
release memory matrix. Targets without SDK ELF section reports are `NOT_RUN`, not
PASS. Host `make memory-budget` remains separate from SDK linker-map validation.

## Commit 6 ATNEL HIL release runners

`tools/hil/hil_release_runner.py` and `tools/fw hil-release-*` commands produce
explicit PASS/FAIL/NOT_RUN reports. PASS requires `EV_HIL_RESULT PASS failures=0
skipped=0` in the serial log. Hardware was not executed during this patch build.

## Commit 7 Wemos minimal-runtime smoke

Wemos smoke tooling and serial markers are present. The current report is
`NOT_RUN` until a physical Wemos ESP-WROOM-02 18650 board is flashed and the
smoke monitor observes the required markers.

## Commit 8 consolidated release report

`tools/release_report.py` writes `docs/release/final_release_validation_summary.md`.
The report uses explicit `PASS`, `FAIL`, `NOT_RUN`, `ENVIRONMENT_BLOCKED` and
`NOT_APPLICABLE` statuses and does not convert missing SDK/HIL runs into PASS.

## ATNEL I2C HIL fault-injection classification

The ATNEL I2C HIL failure is currently classified as a fault-injection fixture
or coupling issue, not a general I2C failure. The captured run passed RTC,
MCP23008, OLED partial flush, OLED full scene flush and missing-device NACK. The
only failed case was `sda-stuck-low-containment`, with `stuck_status=OK` and
`recovery_status=OK`. This indicates that the configured fault GPIO likely did
not pull the real SDA/GPIO5 line low during the fault window. See
`docs/release/hil_atnel_i2c_report.md` and
`docs/hil/fixtures/atnel_i2c_fixture.md` for the wiring checklist.
