# Remote Command Dispatcher

Status: implemented as a bounded host-tested foundation. Physical MQTT command HIL is still required before production use.

## Scope

Remote commands are handled by `ACT_COMMAND`, not by `ACT_NETWORK`. `ACT_NETWORK` remains the transport and telemetry actor. The command actor consumes only MQTT RX events routed through the existing HSHA airlock:

```text
MQTT SDK callback -> bounded network ingress -> app poll -> EV_NET_MQTT_MSG_RX[_LEASE] -> ACT_COMMAND
```

The dispatcher is intentionally small and allowlisted. It supports only these topics:

- `cmd/led`
- `cmd/display`
- `cmd/sleep`

No JSON parser is used. Payloads are bounded ASCII key/value strings separated by semicolons.

## Authentication and capabilities

Commands require `EV_BOARD_NET_COMMAND_TOKEN`. The tracked board profile defaults this token to an empty string, which disables all remote command execution. A private token may be supplied through `board_secrets.local.h` or compile-time definitions.

Commands also require capability bits from `EV_BOARD_REMOTE_COMMAND_CAPABILITIES`:

- `EV_COMMAND_CAP_LED`
- `EV_COMMAND_CAP_DISPLAY`
- `EV_COMMAND_CAP_SLEEP`

No token or no capability means the command is rejected and counted.

## Command grammar

`cmd/led`:

```text
token=<TOKEN>;mask=<hex>;valid=<hex>
```

`cmd/display`:

```text
token=<TOKEN>;text=<printable-ascii>
```

`cmd/sleep` is two-step only:

```text
token=<TOKEN>;arm=<nonce>;ms=<duration_ms>
token=<TOKEN>;confirm=<nonce>
```

A single MQTT packet can never trigger deep sleep. The command actor publishes `EV_SYS_GOTO_SLEEP_CMD` only after a valid arm plus matching confirm inside the bounded confirmation window. The PowerActor remains the final quiescence and sleep-safety gate.

## Safety properties

- No heap allocation.
- No JSON parser.
- No recursion.
- No unbounded `strlen()` on untrusted payloads.
- No `strtok()`.
- No SDK headers in `core/` or `ports/`.
- No cloud-to-local action bypasses `ACT_COMMAND`.
- Invalid token, empty token, missing capability, malformed payload, expired sleep nonce, or rate-limit violation causes a safe drop with diagnostics.

## Verification

Host tests in `tests/host/test_command_actor_contract.c` cover authentication rejects, capability gates, LED/display dispatch, two-step sleep, expiry, rate limiting, and lease-backed MQTT RX handling.

Physical MQTT command HIL is not included in this stage and must be added before production remote-control readiness is claimed.
