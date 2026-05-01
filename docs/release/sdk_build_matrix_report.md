# SDK build matrix report

| Target | Class | Path | Status | Log | Reason |
|---|---|---|---:|---|---|
| `esp8266_generic_dev` | `buildable_sdk` | `adapters/esp8266_rtos_sdk/targets/esp8266_generic_dev` | PASS | `logs/sdk/esp8266_generic_dev/build.log` |  |
| `atnel_air_esp_motherboard` | `buildable_sdk` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard` | PASS | `logs/sdk/atnel_air_esp_motherboard/build.log` |  |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil` | NOT_RUN | `` | hil_sdk excluded; set --include-hil |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_onewire_hil` | NOT_RUN | `` | hil_sdk excluded; set --include-hil |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | `adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_wifi_hil` | NOT_RUN | `` | hil_sdk excluded; set --include-hil |
| `wemos_d1_mini` | `buildable_sdk` | `adapters/esp8266_rtos_sdk/targets/wemos_d1_mini` | PASS | `logs/sdk/wemos_d1_mini/build.log` |  |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | `adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650` | PASS | `logs/sdk/wemos_esp_wroom_02_18650/build.log` |  |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | `adapters/esp8266_rtos_sdk/targets/adafruit_feather_huzzah_esp8266` | NOT_APPLICABLE | `` | metadata-only target |
