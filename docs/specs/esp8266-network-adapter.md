# ESP8266 Network Adapter

The ESP8266 network adapter connects verified ESP8266 RTOS SDK WiFi/MQTT callbacks to the existing HSHA Network Airlock.

## Safety contract

SDK callbacks are asynchronous ingress. They must not call core actors, mailboxes, `ev_publish()`, or domain/system pump APIs directly. Callback code copies bounded metadata into the adapter-owned static ingress ring. The portable app poll loop drains the ring through `ev_net_port_t::poll_ingress()` and publishes `EV_NET_*` events synchronously.

## Bounds and backpressure

The adapter owns a fixed-size power-of-two ingress ring using `EV_NET_INGRESS_RING_CAPACITY` and `EV_NET_INGRESS_RING_MASK`. If the ring is full, the newest event is dropped and `dropped_events` is incremented. MQTT topics and payloads are copied into fixed-size fields in `ev_net_ingress_event_t`; oversize data is dropped and counted in `dropped_oversize`. No SDK pointer is retained in the ring.

## Configuration

The ATNEL target passes WiFi/MQTT configuration from `bsp/atnel_air_esp_motherboard/board_profile.h` into the adapter through `ev_esp8266_net_config_t`. The adapter does not include `board_profile.h` directly, so the ev_platform component remains target-agnostic.

If `EV_BOARD_NET_MQTT_BROKER_URI` is empty, WiFi may still be started but the MQTT client remains disabled and `publish_mqtt` returns unsupported/state errors.


## MQTT build gate

`EV_ESP8266_NET_ENABLE_MQTT` defaults to `0`, so the ESP8266 adapter is
WiFi-only safe and does not require `mqtt_client.h` in the default build.  This
keeps future targets from failing merely because the MQTT SDK component is
absent or disabled.

To enable the MQTT foundation path for an ESP8266 SDK build, define:

```text
-DEV_ESP8266_NET_ENABLE_MQTT=1
```

and ensure the ESP8266 MQTT component/header is available to the SDK build.
MQTT remains inactive unless `EV_BOARD_NET_MQTT_BROKER_URI` is also non-empty.
When the build flag is `0` or the broker URI is empty, `publish_mqtt` rejects TX
with diagnostics and no MQTT SDK APIs are referenced.  This commit still does
not implement telemetry, subscriptions, retained state, or remote commands.

## MQTT foundation policy

This adapter does not route sensor telemetry and does not execute remote
commands. `EV_NET_TX_CMD` is rejected unless an SDK MQTT session is explicitly
connected. MQTT RX events are copied into bounded inline fields and delivered
to `ACT_NETWORK` only as inert ingress events; topic parsing for commands is a
future, separately reviewed commit. Oversize topics or payloads are dropped and
counted.

## Verification status

This document describes code-level integration only. ESP8266 SDK build, WiFi association, MQTT broker connection, and physical network HIL must be run on hardware before production network readiness is claimed.

## Callback/poll shared-state hardening

The ESP8266 adapter protects callback-updated connection state and app-polled
statistics with short critical sections.  SDK callbacks may update local adapter
state and push bounded ingress events, but they must not call `ev_publish()`,
actor handlers, mailbox APIs, or application logic.  The adapter must never hold
its internal critical section while calling SDK functions such as
`esp_wifi_connect()`, `esp_wifi_start()`, `esp_mqtt_client_start()`, or
`esp_mqtt_client_publish()`.

## Reconnect storm policy

WiFi disconnect callbacks are treated as state transitions.  A duplicate
`WIFI_DOWN` event while the adapter is already disconnected is suppressed and
counted in `duplicate_wifi_down_suppressed`.  Reconnect attempts are rate-limited
by `EV_ESP8266_NET_RECONNECT_MIN_INTERVAL_MS`; disconnect callbacks inside that
window increment `reconnect_suppressed` instead of calling `esp_wifi_connect()`.
This bounds reconnect storms and prevents unbounded ingress ring pressure.

## Event-loop ownership

The current ESP8266 adapter owns `esp_event_loop_init()` for this target.  Unknown
event-loop initialization failures are not silently ignored; they increment
`event_loop_init_failures` and fail initialization.  If a future SDK exposes a
verified "already initialized" error code, ownership sharing may be handled in a
separate reviewed commit.

## WiFi reconnect HIL

The physical WiFi adapter is qualified by the dedicated ATNEL HIL target
`atnel_air_esp_motherboard_wifi_hil`. It validates association, AP loss,
recovery, reconnect diagnostics, WDT diagnostic visibility, and callback/poll
heap stability through UART markers parsed by `tools/hil/wifi_reconnect_monitor.py`.

This HIL does not validate MQTT connectivity, telemetry, or remote commands.
Those require later acceptance suites.

## Private network credentials

Tracked board profiles intentionally keep WiFi/MQTT credentials empty.  The
ATNEL profile may include a developer-local `board_secrets.local.h` only when
`EV_BOARD_INCLUDE_LOCAL_SECRETS` is defined by the build.  The local secrets file
is ignored by git and should be created from
`bsp/atnel_air_esp_motherboard/board_secrets.example.h`.

Without local secrets or compile-time overrides, `EV_BOARD_HAS_NET` defaults to
`0U`, so normal host/CI builds do not require private WiFi credentials.  To run
physical WiFi tests, provide local definitions for `EV_BOARD_HAS_NET`,
`EV_BOARD_NET_WIFI_SSID`, and `EV_BOARD_NET_WIFI_PASSWORD`.  MQTT may remain
disabled by leaving `EV_BOARD_NET_MQTT_BROKER_URI` empty.

For the ATNEL ESP8266 SDK target, `main/component.mk` checks for the ignored `board_secrets.local.h` file and auto-defines `EV_BOARD_INCLUDE_LOCAL_SECRETS` only for that local build. This avoids committing credentials while preventing lab builds from silently compiling with `EV_BOARD_HAS_NET=0U` after the secrets file is created.

## SDK build and memory gate

Before enabling MQTT payload pools, telemetry, or remote commands, run the SDK
network build gate:

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard
./tools/fw sdk-network-build-gate
```

The default remains WiFi-only and keeps `EV_ESP8266_NET_ENABLE_MQTT=0`.  A
future MQTT compile check can be requested explicitly by setting the build flag,
but that is not a physical MQTT HIL result.  The gate emits `EV_MEM_*` markers
and does not print WiFi secrets or compiler command lines.

## MQTT payload storage foundation

When `EV_ESP8266_NET_ENABLE_MQTT=1`, MQTT DATA callbacks may receive topics or
payloads that exceed the tiny inline limits used by the first airlock commit.
The adapter therefore uses a static MQTT payload slot pool:

- `EV_NET_MAX_TOPIC_STORAGE_BYTES` defaults to 64 bytes;
- `EV_NET_MAX_PAYLOAD_STORAGE_BYTES` defaults to 128 bytes;
- `EV_NET_PAYLOAD_SLOT_COUNT` defaults to 4 slots.

Callbacks never allocate heap. If a message fits the inline limits, it remains
inline. If it fits the static slot limits, it is copied into a slot and passed
through the ingress ring as a lease-backed payload. If it exceeds the slot
limits or the pool is exhausted, it is dropped with diagnostics.

MQTT remains build-disabled by default. This payload pool does not enable
telemetry or remote commands.

## Outbound telemetry publish view

The portable network port exposes `publish_mqtt_view` for bounded synchronous
telemetry publishes. The view carries caller-owned topic and payload pointers
only for the duration of the call; the ESP8266 adapter copies the topic into a
bounded local buffer before invoking the SDK publish API and must not retain the
view pointers.

Telemetry remains outbound-only and uses QoS 0 with retain=false. The default
ESP8266 build still keeps `EV_ESP8266_NET_ENABLE_MQTT=0`, so telemetry attempts
are counted and dropped until MQTT is explicitly enabled, configured, connected,
and physically qualified. Remote command parsing and MQTT subscriptions remain
out of scope.

## Telemetry publish view

Outbound telemetry uses `ev_net_port_t::publish_mqtt_view` so topics longer than
the original inline `EV_NET_TX_CMD` limit can be published without heap. The
view points to bounded stack buffers owned by `ACT_NETWORK` for the duration of
the synchronous call only. The ESP8266 adapter must copy or transmit the data
before returning and must not retain those pointers.

Telemetry remains disabled unless the actor reaches `EV_NETWORK_STATE_MQTT_CONNECTED`.
The ESP8266 MQTT SDK path is still controlled by `EV_ESP8266_NET_ENABLE_MQTT`.
