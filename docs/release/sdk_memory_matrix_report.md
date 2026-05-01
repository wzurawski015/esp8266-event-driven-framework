# SDK linker-map memory matrix report

PASS requires EV_MEM markers from a real SDK ELF section report. NOT_RUN means no ELF/section report was found.

| Target | Class | Status | IRAM | DRAM | BSS | DATA | Reason |
|---|---|---:|---:|---:|---:|---:|---|
| `esp8266_generic_dev` | `buildable_sdk` | PASS | 22483 | 11100 | 9736 | 1364 | within configured section budgets |
| `atnel_air_esp_motherboard` | `buildable_sdk` | PASS | 22587 | 38700 | 37224 | 1476 | within configured section budgets |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | PASS | 18679 | 9140 | 8096 | 1044 | within configured section budgets |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | PASS | 18679 | 9140 | 8096 | 1044 | within configured section budgets |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | PASS | 26199 | 16220 | 14688 | 1532 | within configured section budgets |
| `wemos_d1_mini` | `buildable_sdk` | PASS | 22483 | 11100 | 9736 | 1364 | within configured section budgets |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | PASS | 22527 | 38612 | 37136 | 1476 | within configured section budgets |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | metadata-only target |
