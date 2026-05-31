# SDK build matrix report

| Target | Class | Status | Log | Reason |
|---|---:|---:|---|---|
| `esp8266_generic_dev` | `buildable_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/esp8266_generic_dev/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard` | `buildable_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/atnel_air_esp_motherboard/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/atnel_air_esp_motherboard_i2c_hil/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/atnel_air_esp_motherboard_onewire_hil/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/atnel_air_esp_motherboard_wifi_hil/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `wemos_d1_mini` | `buildable_sdk` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/wemos_d1_mini/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | ENVIRONMENT_BLOCKED | `docs/release/sdk_evidence/wemos_esp_wroom_02_18650/build.log` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | NOT_APPLICABLE | `docs/release/sdk_evidence/adafruit_feather_huzzah_esp8266/build.log` | metadata-only target |

PASS rows require a committed per-target `evidence.json` and a real build log with SDK markers.
