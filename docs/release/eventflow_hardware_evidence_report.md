# Event-flow hardware evidence report

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Parsed evidence | `docs/release/eventflow_evidence/current/parsed.json` |

| Source | Kind | Status | Required | Evidence | Reason |
|---|---|---:|---:|---|---|
| `sdk_esp8266_generic_dev` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/esp8266_generic_dev/evidence.json` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `sdk_atnel_i2c_hil` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/atnel_air_esp_motherboard_i2c_hil/evidence.json` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `sdk_wemos_smoke` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/wemos_esp_wroom_02_18650/evidence.json` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_i2c_sda_stuck_low` | `hil_i2c` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/i2c/current/parsed.json` | ATNEL I2C hardware fixture or serial log is not available |
| `wemos_smoke` | `hil_wemos_smoke` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/wemos_smoke/current/parsed.json` |  |
| `wemos_deep_sleep_wake` | `hil_wemos_deepsleep` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/wemos_deepsleep/current/parsed.json` |  |

A PASS means real SDK evidence, ATNEL I2C containment evidence, Wemos smoke evidence, and Wemos deep-sleep/wake evidence are all present and parsed as PASS.
Missing hardware remains ENVIRONMENT_BLOCKED by design; it is not converted into PASS.
