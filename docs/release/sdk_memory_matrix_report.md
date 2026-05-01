# SDK linker-map memory matrix report

PASS requires EV_MEM markers from a real SDK ELF section report. NOT_RUN means no ELF/section report was found.

| Target | Class | Status | IRAM | DRAM | BSS | DATA | Reason |
|---|---|---:|---:|---:|---:|---:|---|
| `esp8266_generic_dev` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `atnel_air_esp_motherboard` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `atnel_air_esp_motherboard_i2c_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `atnel_air_esp_motherboard_onewire_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `atnel_air_esp_motherboard_wifi_hil` | `hil_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `wemos_d1_mini` | `buildable_sdk` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `wemos_esp_wroom_02_18650` | `physical_smoke` | NOT_RUN | 0 | 0 | 0 | 0 | no EV_MEM markers found |
| `adafruit_feather_huzzah_esp8266` | `metadata_only` | NOT_APPLICABLE | 0 | 0 | 0 | 0 | metadata-only target |
