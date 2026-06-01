# Route QoS

`config/routes.def` is the single source of truth for publish fan-out. The route
generator accepts both the legacy default form and the explicit policy form:

```c
EV_ROUTE(EVENT_ID, TARGET_ACTOR)
EV_ROUTE_EX(EVENT_ID, TARGET_ACTOR, QOS, PRIORITY, FLAGS)
```

`EV_ROUTE(...)` is intentionally conservative and expands to
`EV_ROUTE_QOS_CRITICAL`, priority `0U`, flags `0U`.

## QoS classes

Supported route QoS values are:

- `EV_ROUTE_QOS_CRITICAL`
- `EV_ROUTE_QOS_BEST_EFFORT`
- `EV_ROUTE_QOS_LOSSY`
- `EV_ROUTE_QOS_COALESCED`
- `EV_ROUTE_QOS_LATEST_ONLY`
- `EV_ROUTE_QOS_WAKEUP_CRITICAL`
- `EV_ROUTE_QOS_TELEMETRY`
- `EV_ROUTE_QOS_COMMAND`

The runtime exposes behavior through one central contract API:

```c
ev_qos_contract_for(qos)
ev_qos_failure_is_drop_allowed(behavior)
ev_qos_validate_route_against_module(route, descriptor, report)
```

## Failure behavior and mailbox semantics

| QoS | Failure behavior | Mailbox semantics |
| --- | --- | --- |
| `EV_ROUTE_QOS_CRITICAL` | strict | Normal enqueue; mailbox rejection is an error. |
| `EV_ROUTE_QOS_WAKEUP_CRITICAL` | strict | Normal enqueue; mailbox rejection is an error. |
| `EV_ROUTE_QOS_COMMAND` | strict | Normal enqueue; mailbox rejection is an error. |
| `EV_ROUTE_QOS_BEST_EFFORT` | drop-allowed | Normal enqueue; full mailbox may be reported as a policy drop. |
| `EV_ROUTE_QOS_LOSSY` | drop-allowed | Normal enqueue; full mailbox may be reported as a policy drop. |
| `EV_ROUTE_QOS_TELEMETRY` | drop-allowed | Normal enqueue; full mailbox may be reported as a policy drop. |
| `EV_ROUTE_QOS_COALESCED` | coalesce, drop-allowed fallback | Repeated pending event id is coalesced without growing queue depth. |
| `EV_ROUTE_QOS_LATEST_ONLY` | replace-latest, drop-allowed fallback | Repeated pending event id is replaced by the newest message. |
| invalid / unknown QoS value | strict | Safer failure mode for corrupted configuration. |

Strict failures return the first delivery error, record `first_error` and
`first_failed_actor`, emit the existing mailbox-overflow fault path, and may stop
further delivery through the current fan-out span. Drop-allowed failures are
reported through `dropped`/`qos_dropped` and metrics, but the publish operation
may still return `EV_OK`.

`COALESCED` and `LATEST_ONLY` are not silent drops. Successful coalescing is
reported through `coalesced`; successful replacement is reported through
`replaced`. Only the bounded fallback for a full mailbox with no matching pending
event is counted as a policy drop.

## Explicitly declared QoS routes

Most routes remain deliberately critical until their semantics are audited. The
current explicit non-default routes are:

- `EV_TIME_UPDATED -> ACT_NETWORK`: telemetry
- `EV_TEMP_UPDATED -> ACT_NETWORK`: telemetry
- `EV_SYS_GOTO_SLEEP_CMD -> ACT_POWER`: wakeup-critical
- `EV_NET_MQTT_MSG_RX -> ACT_COMMAND`: command
- `EV_NET_MQTT_MSG_RX_LEASE -> ACT_COMMAND`: command
- `EV_FAULT_REPORTED -> ACT_FAULT`: critical

Network self-state routes, MQTT state routes, and network transmit commands stay
critical because they participate in actor-local state coherence and
backpressure handling.

## Runtime and mailbox interaction

The delivery path keeps O(fanout) publish behavior by using generated/static
route spans or active route spans. It must not scan all routes for every publish.

QoS compatibility is validated before active delivery. During delivery, the
route QoS is passed to the mailbox through QoS-aware enqueue. The mailbox remains
bounded and does not allocate heap memory.

## Demo compatibility delivery

The demo application keeps a compatibility delivery callback for legacy actor
contexts that still call `ev_publish()`. That callback must consult
`ev_runtime_graph_route_table()` and treat `EV_ACTIVE_ROUTE_OPTIONAL_DISABLED`
as a safe skip. The active route table, not an application-local route engine,
is the source of truth for disabled-route semantics.

## Known limitations

- `route_policy_flags` in `config/modules.def` is historically named as flags,
  but currently behaves as a single accepted QoS class with a few compatibility
  allowances.
- `COALESCED` and `LATEST_ONLY` use bounded same-event-id algorithms. They do
  not perform semantic payload merging across different event ids and do not
  replace arbitrary unrelated queued events.

## End-to-end contract enforcement

The current end-to-end contract is implemented by `ev_qos_contract` and the
QoS-aware mailbox enqueue path:

- `ev_qos_contract_for(qos)` returns the central behavior table.
- `ev_actor_module_route_policy_accepts_qos(descriptor, qos)` documents the historical `route_policy_flags` field as a single accepted QoS class.
- `ev_qos_validate_route_against_module(route, descriptor, report)` runs before active delivery.
- `ev_mailbox_push_qos(mailbox, msg, qos, report)` applies bounded coalescing/replacement/drop semantics.
- Delivery reports expose `rejected_routes`, `qos_conflict_routes`, `coalesced`, `replaced`, `qos_dropped`, and `mailbox_policy_rejected`.
