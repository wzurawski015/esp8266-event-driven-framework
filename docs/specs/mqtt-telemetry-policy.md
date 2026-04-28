# MQTT telemetry publish policy

Status: outbound telemetry foundation only. This specification does not enable
remote commands, Home Assistant discovery, retained state, or MQTT command
subscriptions.

## Source events

The network actor may consume these local domain events and publish compact MQTT
telemetry when the MQTT session is already connected:

| Event | Topic | Payload shape |
|---|---|---|
| `EV_TEMP_UPDATED` | `telemetry/temp` | bounded JSON object with centi-Celsius (`cC`) |
| `EV_TIME_UPDATED` | `telemetry/time` | bounded JSON object with UNIX timestamp |
| `EV_MCP23008_INPUT_CHANGED` | `telemetry/inputs` | bounded JSON object with pressed/changed masks |

Existing local routes remain intact. These events are additionally routed to
`ACT_NETWORK`; the source actors do not know whether the data later leaves the
device.

## Safety policy

Telemetry is outbound only:

- MQTT RX remains foundation-only and is not parsed as a command.
- Remote command topics are intentionally unsupported.
- The network actor must not publish `EV_PANEL_LED_SET_CMD`,
  `EV_OLED_DISPLAY_TEXT_CMD`, or `EV_SYS_GOTO_SLEEP_CMD` in response to MQTT RX.

Telemetry publish attempts use:

```text
qos=0
retain=0
```

If the network actor is not in `EV_NETWORK_STATE_MQTT_CONNECTED`, telemetry is
silently dropped at actor level and `telemetry_dropped_disconnected` is
incremented. This avoids backpressure on local sensor actors.

## Rate limit

Telemetry uses a simple per-class tick rate limit. `EV_TICK_1S` is routed to
`ACT_NETWORK` so the actor can advance an internal telemetry tick counter. A
source class is not published more frequently than
`EV_NETWORK_TELEMETRY_MIN_INTERVAL_TICKS`.

Rate-limited drops increment `telemetry_dropped_rate_limit`.

## Bounded formatting

Topics and payloads are formatted into fixed-size stack buffers controlled by:

```text
EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES
EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES
```

Formatting uses bounded writes and checks the return value. Overflow or invalid
formatting increments `telemetry_dropped_oversize`. No heap is used.

## Publish view

Telemetry topics such as `telemetry/temp` exceed the original tiny inline
`EV_NET_TX_CMD` topic limit. Telemetry therefore uses a synchronous
`ev_net_mqtt_publish_view_t` through `ev_net_port_t::publish_mqtt_view`.

The view is consumed synchronously by the port. The adapter must not retain the
provided topic or payload pointers after the call returns.

## Verification status

This policy is host-tested. Physical MQTT broker connection and MQTT HIL are
not claimed by this specification. MQTT remains opt-in through the ESP8266 build
gate and board/broker configuration.

Remote command execution is not part of telemetry. Authenticated command handling is isolated in `ACT_COMMAND`; see `remote-command-dispatcher.md`.
