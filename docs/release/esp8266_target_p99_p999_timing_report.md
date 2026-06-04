# ESP8266 target P99/P999 timing report

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Reason | No committed Wemos one-shot manifest with target timing evidence is present in this snapshot. |
| Evidence | `docs/release/target_timing/wemos_esp_wroom_02_18650/current/target_timing.json` |

The parser and Makefile gates are present. PASS requires a real ESP8266 serial log with Wemos timing markers and `I (<ms>)` timestamp prefixes. P50/P95/P99/P999 are target-side evidence and are not synthesized from host benchmark results.
