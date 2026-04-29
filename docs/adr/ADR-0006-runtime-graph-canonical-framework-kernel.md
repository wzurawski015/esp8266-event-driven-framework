# ADR-0006: runtime_graph as the canonical framework kernel

## Status

Accepted for staged migration.

## Decision

`ev_runtime_graph_t` is the target canonical framework kernel for route binding,
delivery, scheduling, timers, quiescence, fault/metric storage and trace state.
The existing `apps/demo` composition root remains a compatibility runtime during
this preparation iteration and must not be removed until equivalent behavior is
covered by host and property tests.

## Current migration boundary

This iteration adds actor-instance descriptors, active-route validation,
`runtime_graph` publish/send APIs, a scheduler wrapper around domain/system
pumps, time-aware quiescence, sequence-mask rings and an honest Wemos minimal
runtime target.

The full demo migration is intentionally deferred to the next commit series:

```text
refactor(app): migrate demo app to runtime_graph without losing behavior
```

## Consequences

Future application runtimes must use `runtime_graph` as the owner of framework
mechanisms. `apps/demo` may keep UI/business behavior, but mailbox ownership,
actor runtime ownership, registry binding, route delivery, timers, ingress and
power quiescence must move to framework APIs in the next migration iteration.

## Demo migration outcome

The demo application is migrated to use `runtime_graph` ownership for scheduler, registry, mailboxes and actor runtimes. This ADR remains subject to SDK/HIL confirmation before hardware-production claims.
