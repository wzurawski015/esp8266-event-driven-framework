# SDK real build/map/stack evidence report

This report is generated from per-target evidence JSON files. It does not convert missing SDK toolchain access into PASS.

| Status | Count |
|---|---:|
| PASS | 0 |
| FAIL | 0 |
| ENVIRONMENT_BLOCKED | 7 |
| NOT_RUN | 0 |
| NOT_APPLICABLE | 1 |

| Target | Evidence | SHA manifest |
|---|---|---|
| `esp8266_generic_dev` | `docs/release/sdk_evidence/esp8266_generic_dev/evidence.json` | `docs/release/sdk_evidence/esp8266_generic_dev/sha256sums.txt` |
| `atnel_air_esp_motherboard` | `docs/release/sdk_evidence/atnel_air_esp_motherboard/evidence.json` | `docs/release/sdk_evidence/atnel_air_esp_motherboard/sha256sums.txt` |
| `atnel_air_esp_motherboard_i2c_hil` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_i2c_hil/evidence.json` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_i2c_hil/sha256sums.txt` |
| `atnel_air_esp_motherboard_onewire_hil` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_onewire_hil/evidence.json` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_onewire_hil/sha256sums.txt` |
| `atnel_air_esp_motherboard_wifi_hil` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_wifi_hil/evidence.json` | `docs/release/sdk_evidence/atnel_air_esp_motherboard_wifi_hil/sha256sums.txt` |
| `wemos_d1_mini` | `docs/release/sdk_evidence/wemos_d1_mini/evidence.json` | `docs/release/sdk_evidence/wemos_d1_mini/sha256sums.txt` |
| `wemos_esp_wroom_02_18650` | `docs/release/sdk_evidence/wemos_esp_wroom_02_18650/evidence.json` | `docs/release/sdk_evidence/wemos_esp_wroom_02_18650/sha256sums.txt` |
| `adafruit_feather_huzzah_esp8266` | `docs/release/sdk_evidence/adafruit_feather_huzzah_esp8266/evidence.json` | `docs/release/sdk_evidence/adafruit_feather_huzzah_esp8266/sha256sums.txt` |

Impact: this closes the release-evidence gap only when actual SDK logs are committed. In toolchain-blocked environments the reports remain ENVIRONMENT_BLOCKED by design.

## Canonical capture rule

Generated SDK evidence is trustworthy only when the build log contains target-specific canonical SDK markers and non-zero memory evidence. Memory-report PASS alone does not prove that a target was built.
