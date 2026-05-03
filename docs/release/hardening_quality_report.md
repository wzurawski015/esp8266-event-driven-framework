# Hardening quality report

Baseline archive: `esp8266-event-driven_20260503_081356.tar.gz`.

## Baseline confirmation

- Wemos BSP defaults to `EV_BOARD_HAS_NET=0U`, `EV_BOARD_RUNTIME_PROFILE_MINIMAL=1U`, and `EV_BOARD_RUNTIME_PROFILE_FULL=0U`.
- Wemos network remains opt-in through local secrets and does not claim full hardware runtime by default.
- ATNEL BSP includes `board_secrets.local.h` only when `EV_BOARD_INCLUDE_LOCAL_SECRETS` is defined.
- Repository release artifacts must not contain `*.rej` or `*.orig`.
- Release status remains hardware-incomplete: ATNEL I2C HIL is `FAIL` for `sda-stuck-low-containment`; OneWire HIL, WiFi HIL, and Wemos smoke are `NOT_RUN`.

## Commit 1: zero-heap audit extension

The portable/hot-path static contract now detects `pvPortMalloc`, `vPortFree`, `heap_caps_malloc`, and `heap_caps_free` after comment stripping. The scan also covers host/property tests so framework fixtures cannot hide allocation regressions. Generated/build/log/docker trees remain excluded.

## Commit 2: adapter exception audit

Bootstrap and HIL-only SDK primitives are audited by `tools/audit/adapter_exception_allowlist.def` and summarized in `docs/release/adapter_exception_audit.md`. Unapproved occurrences of `xSemaphoreCreateMutex`, `xSemaphoreCreateBinary`, `xTaskCreate`, `xTaskCreateStatic`, `tcpip_adapter_init`, `esp_wifi_init`, or `esp_mqtt_client_init` fail `make static-contracts`.


## Commit 3: Wemos WiFi opt-in

The Wemos target now supports target-local inclusion of `board_secrets.local.h` through its SDK `component.mk`. This keeps the default no-net minimal runtime unchanged while avoiding global `CFLAGS` overrides. A sanitized `board_secrets.example.h` documents the required local credentials file.


## Commit 4: HIL stack high-water diagnostics

I2C and OneWire IRQ flood HIL tasks emit `EV_HIL_STACK task=irq-flood high_water_words=<n>` when the FreeRTOS high-water API is enabled. The monitor scripts accept and self-test this marker without making it a hardware PASS substitute.


## Commit 5: I2C HIL SDA/SCL diagnostics

The ATNEL I2C HIL target now hard-requires board-profile SDA/SCL GPIO definitions for fault-fixture diagnostics. For this board the bus pins are SDA=GPIO5 and SCL=GPIO4; fault injection remains GPIO12/GPIO13 and the HIL failure status is not hidden.


## Commit 6: release summary tightening

The final release summary now includes a dedicated hardening-contract row and keeps SDK build, SDK memory and physical HIL statuses separate. SDK memory remains `NOT_RUN` when the source archive lacks real `EV_MEM_*` markers. ATNEL I2C HIL remains `FAIL`; OneWire HIL, WiFi HIL and Wemos smoke remain `NOT_RUN` until physical logs provide PASS markers.
