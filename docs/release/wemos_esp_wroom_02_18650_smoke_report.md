# Wemos smoke/deep-sleep evidence report

| Field | Value |
|---|---|
| Status | FAIL |
| Mode | smoke |
| Marker based | False |
| Runtime-alive fallback | False |
| Reason | missing boot marker; missing runtime-ready marker; fewer than 3 tick markers; fewer than 3 snapshot markers |
| Log path | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current/runs/real-wemos-20260604T064940Z/serial.log` |
| Parsed evidence | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current/runs/real-wemos-20260604T064940Z/parsed.json` |

Smoke PASS is either strict marker-based evidence or explicitly marked `runtime_alive_fallback`. Deep-sleep PASS remains strict and requires ordered power state markers plus wake evidence. Placeholder or missing paths are reported as ENVIRONMENT_BLOCKED, not Python tracebacks.
