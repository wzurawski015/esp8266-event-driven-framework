# QoS delivery contract

Route QoS is an end-to-end contract between route configuration, actor-module route policy, mailbox behavior, delivery reports, and release evidence.

| QoS | Failure behavior | Module policy meaning |
|---|---|---|
| `EV_ROUTE_QOS_CRITICAL` | strict | Accepted by default modules and as compatibility traffic by specialized modules. |
| `EV_ROUTE_QOS_WAKEUP_CRITICAL` | strict, wakeup relevant | Accepted only by wakeup-policy modules. |
| `EV_ROUTE_QOS_COMMAND` | strict, command relevant | Accepted only by command-policy modules. |
| `EV_ROUTE_QOS_BEST_EFFORT` | drop allowed | Accepted by default and telemetry-policy modules. |
| `EV_ROUTE_QOS_LOSSY` | drop allowed | Accepted by default and telemetry-policy modules. |
| `EV_ROUTE_QOS_TELEMETRY` | drop allowed, telemetry relevant | Accepted only by telemetry-policy modules. |
| `EV_ROUTE_QOS_COALESCED` | algorithm-not-yet-promoted | drop-allowed until a mailbox replacement algorithm is promoted. |
| `EV_ROUTE_QOS_LATEST_ONLY` | algorithm-not-yet-promoted | drop-allowed until a mailbox replacement algorithm is promoted. |

`route_policy_flags` is a historical ABI field and is treated as a single accepted route QoS class, not a bitmask. The semantic accessor is `ev_actor_module_route_policy_accepts_qos()`.
