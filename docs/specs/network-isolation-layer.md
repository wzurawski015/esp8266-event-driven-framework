# Network Isolation Layer

Status: initial bounded HSHA airlock scaffold.  This document describes the
portable contract, host fakes, actor policy, and adapter boundary.  It does not
claim ESP8266 WiFi/MQTT hardware validation.

## HSHA boundary

The network layer must keep SDK callbacks out of the synchronous actor graph.
The only allowed flow is:

```text
SDK WiFi/MQTT callback
  -> bounded adapter ingress ring / fixed-size payload copy
  -> app poll ingress budget
  -> EV_NET_* publication
  -> ACT_NETWORK
```

Callbacks must not call `ev_publish`, actor handlers, mailbox APIs, or domain
logic directly.

## Bounded ingress and drop policy

`ev_net_port_t` exposes a bounded `poll_ingress` operation.  Adapter and fake
callback paths must push only into a power-of-two ring.  If the ring is full,
the newest event is dropped and `dropped_events` is incremented.  Oversize MQTT
topics or payloads are dropped and counted as `dropped_oversize`.

Current fixed limits are intentionally small for inline-message safety:

- `EV_NET_INGRESS_RING_CAPACITY`
- `EV_NET_MAX_TOPIC_BYTES`
- `EV_NET_MAX_INLINE_PAYLOAD_BYTES`

No heap allocation is allowed in callback, poll, actor, or publish paths.

## Actor policy

`ACT_NETWORK` lives in `EV_DOMAIN_SLOW_IO`.  The actor implements only a small
state machine in this commit:

```text
DISCONNECTED -> WIFI_UP -> MQTT_CONNECTED
```

`EV_NET_TX_CMD` calls `ev_net_port_t::publish_mqtt` only when MQTT is connected.
Reconnect loops, TLS, broker sessions, and production SDK integration are out of
scope for this commit.


## ESP8266 MQTT build gate

The ESP8266 physical adapter is intentionally WiFi-only by default.
`EV_ESP8266_NET_ENABLE_MQTT` defaults to `0`; in that mode the adapter must not
include or call the SDK MQTT client API.  Host/fake tests may still exercise
portable MQTT-like events through `ev_net_port_t`, because the airlock contract is
independent of the ESP8266 MQTT component.

A later MQTT qualification step may compile with `EV_ESP8266_NET_ENABLE_MQTT=1`
after verifying SDK component availability and broker configuration.  Telemetry
routes and remote command parsing remain out of scope.

## ESP8266 adapter status

The ESP8266 adapter is a bounded WiFi/MQTT foundation behind this airlock.
WiFi station callbacks push `EV_NET_WIFI_UP` / `EV_NET_WIFI_DOWN` into the
static ingress ring. MQTT is intentionally foundation-only at this stage:
when the BSP broker URI is empty the adapter keeps MQTT disabled and rejects
TX with diagnostics; when a broker is configured, SDK MQTT callbacks may only
push bounded `EV_NET_*` ingress events. This layer still does not implement
telemetry routing, Home Assistant discovery, or remote command execution.
Physical WiFi/MQTT HIL remains required before production network readiness is
claimed.

## Verification requirements for future hardware adapter

A production adapter must prove:

- SDK callbacks never block,
- SDK callbacks never call core directly,
- RX flood increments drop counters instead of starving the pump,
- WDT health does not remain true under network-induced stalls,
- no heap is used in callback or actor hot paths,
- WiFi/MQTT HIL stress passes with stored logs.

## Static MQTT payload lease pool

The Network Airlock now supports two MQTT RX payload forms:

- **Inline payloads** remain bounded by `EV_NET_MAX_TOPIC_BYTES` and
  `EV_NET_MAX_INLINE_PAYLOAD_BYTES` and are carried as inline message payloads.
- **Static-slot payloads** use a fixed-size pool controlled by
  `EV_NET_PAYLOAD_SLOT_COUNT`, `EV_NET_MAX_TOPIC_STORAGE_BYTES`, and
  `EV_NET_MAX_PAYLOAD_STORAGE_BYTES`.

Ownership is deterministic and zero-heap:

1. the adapter/fake callback acquires one static slot before committing an event
   to the ingress ring;
2. if the ring push fails, the slot is released immediately;
3. after app ingress publishes the event, normal message disposal releases the
   slot through the payload release callback;
4. oversize topics or payloads are dropped and counted through
   `dropped_oversize`;
5. pool exhaustion is dropped and counted through `dropped_no_payload_slot`.

This is still a foundation layer. It does not add telemetry routes, remote
commands, subscriptions that execute local actions, or MQTT production HIL.

## Outbound MQTT telemetry policy

`ACT_NETWORK` may publish outbound telemetry for selected local domain events only:

- `EV_TEMP_UPDATED` -> `telemetry/temp`
- `EV_TIME_UPDATED` -> `telemetry/time`
- `EV_MCP23008_INPUT_CHANGED` -> `telemetry/inputs`

Telemetry is outbound-only. MQTT RX remains foundation-only and does not execute
local actions. Telemetry uses QoS 0 and retain=false. If MQTT is disconnected,
unsupported, disabled, rate-limited, or if bounded formatting would overflow the
static buffers, the event is dropped with counters instead of blocking the actor
or growing mailboxes.

Telemetry topics and payloads are passed through `ev_net_mqtt_publish_view_t`, a
synchronous port view consumed during the call. The adapter must not retain the
view pointers after `publish_mqtt_view` returns. This keeps the inline
`EV_NET_TX_CMD` contract small while allowing longer fixed telemetry topics.

## Outbound telemetry policy

`ACT_NETWORK` may publish outbound telemetry for selected local domain events:
`EV_TEMP_UPDATED`, `EV_TIME_UPDATED`, and `EV_MCP23008_INPUT_CHANGED`. The
additional routes preserve the local consumers and add `ACT_NETWORK` as a
bounded telemetry policy actor.

Telemetry is strictly outbound, QoS 0, retain=false, and rate-limited. MQTT RX
remains foundation-only and cannot execute local commands. Larger telemetry
topics use `ev_net_mqtt_publish_view_t`, which is consumed synchronously by the
network port without retaining pointers.

Remote commands are handled by a separate `ACT_COMMAND`; SDK callbacks still enter only through the bounded network airlock. See `remote-command-dispatcher.md`.
