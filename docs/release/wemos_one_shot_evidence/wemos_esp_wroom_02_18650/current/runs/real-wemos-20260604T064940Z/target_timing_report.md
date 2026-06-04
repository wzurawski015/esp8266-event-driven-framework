# ESP8266 target P99/P999 timing evidence

| Field | Value |
|---|---|
| Status | INSUFFICIENT_SAMPLES |
| Target | wemos_esp_wroom_02_18650 |
| Source log | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current/runs/real-wemos-20260604T064940Z/serial.raw.log` |
| Source SHA-256 | `13eec975f84c4d1d0ec442df2fe3e70b0c5d1999a1883fb28fe0d7523294e9b3` |
| Samples | 0 |
| P50/P95/P99/P999 ms | None / None / None / None |
| Reason | insufficient target timing samples: 0 < 8 |

| Metric | Count | P50 ms | P95 ms | P99 ms | P999 ms | Max |
|---|---:|---:|---:|---:|---:|---:|

P99/P999 are target-side evidence. They do not replace Wemos smoke, SDK build, flash or deep-sleep PASS.
