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
