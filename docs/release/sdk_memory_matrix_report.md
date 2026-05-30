# SDK linker-map memory matrix report

PASS requires EV_MEM markers from a real SDK ELF section report. NOT_RUN means no ELF/section report was found.
APP_BIN is checked against `max_app_bin_bytes` from `config/sdk_memory_budgets.def` when a real report provides the application binary size.
STACK is a report-only per-function `.su` max-frame baseline; it is not a call-chain worst-case proof.
Strict release mode is enabled with `EV_SDK_MEMORY_REQUIRE_PASS=1` and fails on non-metadata rows that are not PASS.

| Target | Class | Status | IRAM | DRAM | BSS | DATA | APP_BIN | STACK | Reason |
|---|---|---:|---:|---:|---:|---:|---:|---|---|
| `esp8266_generic_dev` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `atnel_air_esp_motherboard` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `wemos_d1_mini` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | NOT_RUN | 0 | 0 | 0 | 0 | 0 | N/A | no EV_MEM markers found |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | 0 | N/A | metadata-only target |

Strict mode: FAIL
