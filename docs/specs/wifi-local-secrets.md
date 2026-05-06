# Local WiFi credentials workflow

This project keeps WiFi credentials out of tracked files.  Supported WiFi-capable
SDK targets read a developer-local `board_secrets.local.h` from the relevant BSP
folder when the target-local `component.mk` opts in with
`EV_BOARD_INCLUDE_LOCAL_SECRETS`.

## Supported target-local secret files

| Board | Local secret file | Main SDK target | HIL target |
|---|---|---|---|
| ATNEL AIR ESP Motherboard | `bsp/atnel_air_esp_motherboard/board_secrets.local.h` | `atnel_air_esp_motherboard` | `atnel_air_esp_motherboard_wifi_hil` |
| Wemos ESP-WROOM-02 18650 | `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h` | `wemos_esp_wroom_02_18650` | Wemos smoke |

The local files are ignored by Git.  Do not commit credentials, logs containing
credentials, or generated local secret headers.

## Creating local secrets

Preferred interactive workflow:

```bash
cd ~/esp8266-event-driven-framework
unset CFLAGS CPPFLAGS LDFLAGS
export EV_WIFI_SSID='your-2g4-ssid'
./tools/fw wifi-secrets-all
```

The command prompts for the password without printing it.  For CI labs or a
throwaway local shell, `EV_WIFI_PASSWORD` may be supplied explicitly:

```bash
EV_WIFI_SSID='your-2g4-ssid' EV_WIFI_PASSWORD='your-password' ./tools/fw wifi-secrets-atnel
EV_WIFI_SSID='your-2g4-ssid' EV_WIFI_PASSWORD='your-password' ./tools/fw wifi-secrets-wemos
```

Check state without revealing passwords:

```bash
./tools/fw wifi-secrets-status
```

## Build and flash ATNEL WiFi runtime

```bash
export FW_ESPPORT=/dev/ttyUSB0
./tools/fw atnel-wifi-build
./tools/fw atnel-wifi-flash
./tools/fw atnel-wifi-monitor
```

The formal WiFi HIL release workflow is separate:

```bash
FW_ESPPORT=/dev/ttyUSB0 ./tools/fw hil-release-atnel-wifi
cat docs/release/hil_atnel_wifi_report.md
```

## Build and flash Wemos WiFi runtime

```bash
export FW_ESPPORT=/dev/ttyUSB1
./tools/fw wemos-wifi-build
./tools/fw wemos-wifi-flash
./tools/fw wemos-wifi-monitor
```

Wemos remains no-net by default.  WiFi is enabled only when the ignored local
secret header exists and defines `EV_BOARD_HAS_NET 1U`.

## Guardrails

- Do not set global `CFLAGS=-DEV_BOARD_INCLUDE_LOCAL_SECRETS=1`; it overrides
  Makefile include paths and breaks host builds.
- ESP8266 supports 2.4 GHz WiFi only.
- Leave MQTT and command credentials empty unless a dedicated HIL/release test
  requires them.
- HIL PASS requires a serial marker such as `EV_HIL_RESULT PASS failures=0 skipped=0`.
