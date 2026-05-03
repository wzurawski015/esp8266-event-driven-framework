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
