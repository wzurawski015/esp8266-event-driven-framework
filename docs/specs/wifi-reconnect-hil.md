# WiFi reconnect HIL acceptance

This specification defines the ATNEL AIR ESP Motherboard WiFi association and
reconnect hardware-in-the-loop acceptance suite.

The suite validates physical WiFi only. MQTT connectivity, telemetry, retained
state, subscriptions, and remote commands are out of scope.

## Required result

A qualifying run must end with:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

Skipped phases are failures. A UART log from the monitor must be retained for a
release qualification.

## Phases

1. `boot-connect`: boot, connect to the configured AP, observe `EV_NET_WIFI_UP`.
2. `ap-loss`: operator disables or isolates the AP; HIL must observe
   `EV_NET_WIFI_DOWN`, bounded reconnect attempts, bounded ring high-watermark,
   and reconnect diagnostics.
3. `recovery`: operator restores the AP; HIL must observe `EV_NET_WIFI_UP` after
   the down event.
4. `wdt-health-under-net-storm`: HIL emits WDT health diagnostics. If WDT is not
   enabled by the board profile, the log explicitly reports `enabled=0`.
5. `heap-delta-window`: HIL records free-heap before and after a bounded network
   poll window. This separates callback/poll path observation from SDK-owned
   boot/connect allocation.

## Machine-readable UART markers

The firmware and monitor use these markers:

```text
EV_HIL_START wifi-reconnect
EV_HIL_PHASE <name>
EV_HIL_WIFI_UP ip=<...> rssi=<...>
EV_HIL_WIFI_DOWN reason=<...>
EV_HIL_NET_STATS write_seq=<...> read_seq=<...> pending=<...> dropped=<...> high_watermark=<...> reconnect_attempts=<...> reconnect_suppressed=<...>
EV_HIL_WDT_STATS enabled=<0|1> feeds_ok=<...> health_rejects=<...> feeds_failed=<...>
EV_HIL_HEAP before=<...> after=<...> delta=<...>
EV_HIL_RESULT PASS failures=0 skipped=0
```

If IP/RSSI APIs are not available in the current SDK context, the firmware logs
`ip=unavailable rssi=unavailable` rather than fabricating values.

## Running

```sh
./tools/fw hil-wifi-build
./tools/fw hil-wifi-flash
./tools/fw hil-wifi-monitor
# or
./tools/fw hil-wifi-run
```

The `ap-loss` and `recovery` phases require an operator-controlled AP/router.

## Local credentials for HIL

The WiFi HIL target requires credentials but tracked BSP files do not store
private secrets.  Copy the ATNEL example file:

```text
bsp/atnel_air_esp_motherboard/board_secrets.example.h
```

to:

```text
bsp/atnel_air_esp_motherboard/board_secrets.local.h
```

then build with `EV_BOARD_INCLUDE_LOCAL_SECRETS` defined, or pass equivalent
compile-time `-D` overrides from the lab build environment.  The local secrets
file is ignored by git.  Leave `EV_BOARD_NET_MQTT_BROKER_URI` empty for WiFi-only
HIL.

The ATNEL WiFi HIL target build auto-defines `EV_BOARD_INCLUDE_LOCAL_SECRETS` when the ignored local header exists. A missing local header is treated as a secret-free build, so `EV_BOARD_HAS_NET` remains disabled and WiFi HIL cannot be qualified until credentials are supplied.
