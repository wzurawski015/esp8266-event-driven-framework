# QoS delivery contract

Route QoS is an end-to-end contract between route configuration, actor-module
route policy, mailbox behavior, delivery reports, metrics, and release evidence.
The contract is resolved before the hot path for route/module compatibility and
then applied by QoS-aware mailbox enqueue during delivery.

| QoS | Failure behavior | Module policy meaning | Mailbox delivery semantics |
|---|---|---|---|
| `EV_ROUTE_QOS_CRITICAL` | strict | Accepted by default modules and as compatibility traffic by specialized modules. | Normal enqueue; mailbox failure is a delivery error. |
| `EV_ROUTE_QOS_WAKEUP_CRITICAL` | strict, wakeup relevant | Accepted only by wakeup-policy modules. | Normal enqueue; mailbox failure is a delivery error. |
| `EV_ROUTE_QOS_COMMAND` | strict, command relevant | Accepted only by command-policy modules. | Normal enqueue; mailbox failure is a delivery error. |
| `EV_ROUTE_QOS_BEST_EFFORT` | drop allowed | Accepted by default and telemetry-policy modules. | Normal enqueue; mailbox full may be visible as a policy drop. |
| `EV_ROUTE_QOS_LOSSY` | drop allowed | Accepted by default and telemetry-policy modules. | Normal enqueue; mailbox full may be visible as a policy drop. |
| `EV_ROUTE_QOS_TELEMETRY` | drop allowed, telemetry relevant | Accepted only by telemetry-policy modules. | Normal enqueue; mailbox full may be visible as a policy drop. |
| `EV_ROUTE_QOS_COALESCED` | coalesce, drop allowed fallback | Accepted where module policy allows coalesced traffic. | If an unconsumed message with the same event id is already pending, the new message is coalesced and queue depth does not grow. |
| `EV_ROUTE_QOS_LATEST_ONLY` | replace latest, drop allowed fallback | Accepted where module policy allows latest-only traffic. | If an unconsumed message with the same event id is already pending, the old slot is safely disposed and replaced with the newest message. |

`route_policy_flags` is a historical ABI field and is treated as a single
accepted route QoS class, not a bitmask. The semantic accessor is
`ev_actor_module_route_policy_accepts_qos()`.

## Bounded mailbox algorithms

`COALESCED` and `LATEST_ONLY` are implemented without heap allocation and
without changing mailbox capacities. They operate only on the bounded pending
slots of the target actor mailbox.

For `EV_ROUTE_QOS_COALESCED`, a repeated event id already pending in the target
mailbox is considered delivered by coalescing. The incoming message is not
retained, the existing slot remains the representative pending work item, and
the delivery report increments `coalesced` rather than `dropped`.

For `EV_ROUTE_QOS_LATEST_ONLY`, a repeated event id already pending in the
target mailbox is replaced by the newer message. The implementation retains the
new message before modifying the old slot. If retain fails, the old slot remains
intact. When replacement succeeds, the old slot is disposed exactly once and the
delivery report increments `replaced`.

If no matching event id exists and the mailbox is full, both QoS classes use the
explicit drop-allowed fallback. The drop is visible through the mailbox delivery
report, delivery report counters, and QoS drop metrics.
