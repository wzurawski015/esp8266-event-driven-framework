# Route QoS

The generated route table now supports:

```c
EV_ROUTE_EX(EVENT_ID, TARGET_ACTOR, QOS, PRIORITY, FLAGS)
```

The legacy macro remains supported:

```c
EV_ROUTE(EVENT_ID, TARGET_ACTOR)
```

Supported QoS values:

- `EV_ROUTE_QOS_CRITICAL`
- `EV_ROUTE_QOS_BEST_EFFORT`
- `EV_ROUTE_QOS_LOSSY`
- `EV_ROUTE_QOS_COALESCED`
- `EV_ROUTE_QOS_LATEST_ONLY`
- `EV_ROUTE_QOS_WAKEUP_CRITICAL`
- `EV_ROUTE_QOS_TELEMETRY`
- `EV_ROUTE_QOS_COMMAND`

The delivery service updates metrics, emits faults on critical overflow, and records trace entries when tracing is enabled.

## Demo compatibility delivery

The demo application keeps a compatibility delivery callback for legacy actor contexts that still call `ev_publish()`. That callback must consult `ev_runtime_graph_active_routes()` and treat `EV_ACTIVE_ROUTE_OPTIONAL_DISABLED` as a safe skip. The active route table, not an application-local route engine, is the source of truth for disabled-route semantics.
