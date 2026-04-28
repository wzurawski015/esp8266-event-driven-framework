# ADR-0003: Routing and delivery model

- Status: Accepted
- Date: 2026-03-14

## Context

The framework is intentionally event-driven, but blind broadcast pub/sub is too expensive and too error-prone for constrained embedded systems.
The routing model must be deterministic, reviewable, and bounded.

## Decision

We adopt **two** explicit delivery paths:

1. `ev_send(target_actor, msg)`
   - Direct, point-to-point delivery.
   - Used for commands, control signals, and request/response paths.
   - The caller supplies the target actor explicitly.

2. `ev_publish(msg)`
   - Static fan-out delivery.
   - Used for domain and system events.
   - Delivery targets are resolved only through `config/routes.def`.

## Routing rules

- No runtime subscribe/unsubscribe.
- No string-based topics.
- No wildcard listeners.
- No implicit global broadcast.
- No hidden self-delivery: if an actor must receive an event, the route must exist explicitly.
- Duplicate routes for the same `(event_id, target_actor)` pair are forbidden.

## Delivery semantics

### `ev_send()`

- Requires a valid target actor.
- Bypasses the publish route table.
- Inserts the message into exactly one mailbox.
- Fails deterministically if the target mailbox cannot accept the message.

### `ev_publish()`

- Requires `target_actor == EV_ACTOR_NONE`.
- Looks up a compile-time route list for `event_id`.
- Delivers only to actors listed for that event.
- Fan-out is finite and reviewable.
- If an event has no route, the behavior is explicit and testable; it must not silently become a global broadcast.

## Ordering policy

- Ordering is defined **per destination mailbox**, not globally across the system.
- If two messages are accepted into the same FIFO mailbox in order `A` then `B`, that mailbox must present them in the same order.
- Cross-actor relative ordering is not guaranteed unless a higher-level protocol defines it.

## Consequences

### Positive

- Routing cost is bounded.
- Delivery graphs can be generated directly from SSOT.
- Memory accounting is easier.
- Event fan-out is explicit in code review and documentation.

### Trade-offs

- Less runtime flexibility than generic pub/sub.
- All routing changes require source changes and review.

## Follow-up

Implementation should generate a static route table from `config/routes.def` and expose host-side tests proving:

- every target actor exists,
- every route references a valid event,
- duplicate routes are rejected,
- publish never degenerates into broadcast.

## Failure handling policy

Publish fan-out is not treated as an unstructured broadcast.
The core distinguishes two policies:

- fail-fast for command-like or strongly ordered publication,
- best-effort for observability and multi-subscriber fan-out.

The accounting report for one publish attempt must expose how many static routes matched,
how many deliveries were attempted, how many succeeded, how many failed, and the first failure code.
This makes partial delivery explicit and testable.
