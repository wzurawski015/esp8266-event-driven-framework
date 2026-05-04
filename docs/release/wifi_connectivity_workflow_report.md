# WiFi connectivity workflow report

This report records the project-side changes that make WiFi connection setup
repeatable without committing credentials.

## Scope

- Adds a local-secret generator for supported WiFi-capable boards.
- Adds `tools/fw` wrappers for ATNEL and Wemos WiFi build/flash/monitor flows.
- Keeps tracked defaults secret-free and Wemos no-net by default.

## Supported boards

| Board | Status |
|---|---|
| ATNEL AIR ESP Motherboard | WiFi-capable when `bsp/atnel_air_esp_motherboard/board_secrets.local.h` exists |
| Wemos ESP-WROOM-02 18650 | WiFi-capable opt-in when `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h` exists |

## Validation status

This patch does not claim hardware WiFi PASS.  It enables safe local credential
creation and target build/flash workflows.  A board is validated only after the
serial log shows the relevant WiFi/HIL PASS markers.
