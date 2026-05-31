# Route QoS

`config/routes.def` is the single source of truth for publish fan-out. The
route generator accepts both the legacy default form and the explicit policy
form:

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

The runtime delivery service exposes the failure behavior through one central
policy API:

```c
ev_delivery_qos_failure_policy(qos)
ev_delivery_qos_failure_is_drop(qos)
ev_delivery_qos_failure_is_strict(qos)
```

## Failure behavior contract

Delivery failure here means that a selected route target cannot accept a message,
for example because the target mailbox rejected `ev_mailbox_push()`.

| QoS | Failure behavior |
| --- | --- |
| `EV_ROUTE_QOS_CRITICAL` | strict |
| `EV_ROUTE_QOS_WAKEUP_CRITICAL` | strict |
| `EV_ROUTE_QOS_COMMAND` | strict |
| `EV_ROUTE_QOS_BEST_EFFORT` | drop-allowed |
| `EV_ROUTE_QOS_LOSSY` | drop-allowed |
| `EV_ROUTE_QOS_COALESCED` | drop-allowed |
| `EV_ROUTE_QOS_LATEST_ONLY` | drop-allowed |
| `EV_ROUTE_QOS_TELEMETRY` | drop-allowed |
| invalid / unknown QoS value | strict |

Invalid QoS values are treated as strict. This is the safer failure mode because
configuration corruption must not silently discard mandatory control traffic.

Strict failures return the first delivery error, record `first_error` and
`first_failed_actor`, emit the existing mailbox-overflow fault path, and may stop
further delivery through the current fan-out span. Drop-allowed failures are
reported through `dropped` and metrics, but the publish operation may still
return `EV_OK`.

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
critical in this patch because they participate in actor-local state coherence
and backpressure handling.

## Runtime and mailbox interaction

This patch does not add a new algorithm for coalescing or latest-only delivery.
Those classes currently inherit drop-allowed failure behavior, while concrete
mailbox behavior is still determined by the target actor mailbox kind.

The delivery path keeps O(fanout) publish behavior by using generated/static
route spans or active route spans. It must not scan all routes for every publish.

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
- `COALESCED` and `LATEST_ONLY` do not yet have additional delivery algorithms
  beyond the existing mailbox kind behavior and the drop-allowed failure policy.
- Delivery trace timestamps are still outside this contract and currently remain
  part of the trace timestamp follow-up work.

## End-to-end contract enforcement

The current end-to-end contract is implemented by `ev_qos_contract`:

- `ev_qos_contract_for(qos)` returns the central behavior table.
- `ev_actor_module_route_policy_accepts_qos(descriptor, qos)` documents the historical `route_policy_flags` field as a single accepted QoS class.
- `ev_qos_validate_route_against_module(route, descriptor, report)` runs before active delivery.
- Delivery reports expose `rejected_routes` and `qos_conflict_routes` so partial rejection is visible.

`EV_ROUTE_QOS_COALESCED` and `EV_ROUTE_QOS_LATEST_ONLY` are explicit `algorithm-not-yet-promoted` classes in this patch. They remain drop-allowed until a bounded mailbox replacement/coalescing algorithm is promoted with its own tests and performance evidence.
