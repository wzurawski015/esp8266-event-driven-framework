# SDK memory matrix report

| Target | Class | Status | IRAM | DRAM | BSS | DATA | APP_BIN | Stack | Reason |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| `esp8266_generic_dev` | `buildable_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard` | `buildable_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `wemos_d1_mini` | `buildable_sdk` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | ENVIRONMENT_BLOCKED | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | 0 | STACK_NOT_AVAILABLE:0 | metadata-only target |

Strict mode: FAIL unless required rows have real EV_MEM markers and budgets satisfied.
