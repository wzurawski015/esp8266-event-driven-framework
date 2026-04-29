# Architecture overview

The framework is organized around a deterministic actor runtime with bounded mailboxes and generated route tables. The design follows a Half-Sync/Half-Async decomposition: platform adapters and interrupt-adjacent code capture asynchronous ingress into bounded queues, while framework actors consume events through cooperative, bounded polling.

The reusable framework mechanisms are in `runtime/`:

- `ev_runtime_builder` builds a graph from static module descriptors.
- `ev_timer_service` owns one-shot and periodic deadlines.
- `ev_ingress_service` collects bounded external ingress.
- `ev_quiescence_service` reports whether the graph is safe to idle or sleep.
- `ev_delivery_service` applies route QoS and delivery policy.
- `ev_power_manager` evaluates framework-level power policy.
- `ev_fault_bus`, `ev_metrics_registry`, and `ev_trace_ring` provide diagnostic infrastructure.

The previous monolithic demo application role has been split. Application code now provides wiring and behavior, while runtime coordination lives in framework services.

## Canonical runtime direction

`runtime_graph` is the target framework kernel. During the preparation
iteration, `apps/demo` remains a compatibility runtime while framework-level
route binding, scheduler, quiescence and sequence-ring primitives are hardened.

## Canonical demo runtime

`apps/demo` delegates runtime ownership to `ev_runtime_graph_t`. Demo-specific code owns actor contexts and application behavior; framework runtime primitives remain in `runtime/`.
