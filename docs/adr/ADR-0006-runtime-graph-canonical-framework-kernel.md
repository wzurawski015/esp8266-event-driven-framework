# ADR-0006: runtime_graph as the canonical framework kernel

## Status

Accepted. Demo runtime ownership migration is complete for host-validated code.
SDK and HIL confirmation remain outside this ADR until executed on the target
toolchain and hardware.

## Decision

`ev_runtime_graph_t` is the canonical framework kernel for route binding,
delivery, scheduling, timers, quiescence, fault/metric storage and trace state.
The demo application now owns application state and actor contexts, while
mailboxes, actor runtimes, actor registry, route binding, scheduler ownership
and timer ownership are framework-owned.

## Current migration boundary

The previous manual demo-owned runtime was removed in the migration series.
Hard static contracts prevent `apps/demo` from reintroducing per-actor
mailboxes, actor runtimes, registry, domain/system pumps or legacy tick fields.

This hardening iteration removes the remaining compatibility seams:

- direct demo access to `graph.scheduler` and `graph.timer_service`,
- demo-owned poll orchestration,
- demo delivery callback as actor emission path,
- incomplete route/delivery fault and metric emission,
- dead `pending_log_records` quiescence reporting without a log pending hook.

## Consequences

Future application runtimes must call public `runtime_graph` and `runtime_loop`
APIs rather than inspecting framework internals. Actor emission must use a
framework publish/send port, not an application-specific delivery callback.

## Remaining validation boundary

The architecture is host-gated. SDK matrix builds, HIL on ATNEL I2C/OneWire/WiFi,
Wemos minimal-runtime smoke on a real board and final linker-map RAM accounting
remain required before hardware-production claims.
