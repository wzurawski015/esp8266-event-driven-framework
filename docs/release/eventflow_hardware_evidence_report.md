# Event-flow hardware evidence report

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Parsed evidence | `docs/release/eventflow_evidence/current/parsed.json` |
| Sequence | SDK build -> ATNEL I2C containment -> Wemos boot/runtime/tick -> sleep request -> deep-sleep enter -> wake boot |

| Source | Kind | Status | Required | Evidence | SHA-256 | Reason |
|---|---|---:|---:|---|---|---|
| `sdk_esp8266_generic_dev` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/esp8266_generic_dev/evidence.json` | `771a9918feefbb1b4ae0cb7227a2f7918b35522da76ca7beedd64d1731d9a312` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `sdk_atnel_i2c_hil` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/atnel_air_esp_motherboard_i2c_hil/evidence.json` | `ec751f571ef87509bce914f84b2a534a0be53fe23f8eb7c1c23c182abd36ad90` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `sdk_wemos_smoke` | `sdk` | ENVIRONMENT_BLOCKED | True | `docs/release/sdk_evidence/wemos_esp_wroom_02_18650/evidence.json` | `962d0f6d7e289f7b14aed0a9acf38abe7677f0c9aeb9b02fe4558d2668db34a3` | SDK build toolchain not available: neither IDF_PATH nor docker runner found |
| `atnel_i2c_sda_stuck_low` | `hil_i2c` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/i2c/current/parsed.json` | `aa3208d9b26c4a1f4ea2ae553884f972bbeac466b8a588cca23ff17c591b2845` | ATNEL I2C hardware fixture or serial log is not available |
| `wemos_smoke` | `hil_wemos_smoke` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/wemos_smoke/current/parsed.json` | `59218cd66132d393adb2f762ddfe358d0ef4c293554eb74b5c00a351d4108c4f` |  |
| `wemos_deep_sleep_wake` | `hil_wemos_deepsleep` | ENVIRONMENT_BLOCKED | True | `docs/release/hil_evidence/wemos_deepsleep/current/parsed.json` | `9ccf580538a18f2398e61ee1ba9054efecf48f2dcd493d0c2e0f2f84627730bd` |  |

A PASS means all required real SDK and HIL sources are present and parsed as PASS. `ENVIRONMENT_BLOCKED` is preserved when hardware or SDK evidence is missing.
