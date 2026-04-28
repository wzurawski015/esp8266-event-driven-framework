# ADR-0002: Message contract

- Status: Accepted
- Date: 2026-03-14

## Context

The framework needs one canonical transport envelope that is small, explicit, deterministic, and suitable for static routing.
The message contract must work for both point-to-point commands and fan-out domain events.
It must also be compatible with zero-heap hot paths and strict ownership rules.

## Decision

We define a single canonical runtime message type, `ev_msg`, with the following conceptual parts:

1. **Header**
   - `event_id`: catalog-defined event identifier.
   - `source_actor`: sender identity.
   - `target_actor`: explicit receiver for `ev_send()`, or `EV_ACTOR_NONE` for `ev_publish()`.
   - `flags`: delivery and lifecycle flags.

2. **Payload descriptor**
   - The payload storage model is determined by the event catalog.
   - The envelope carries the data needed to access the payload, not a hidden heap object.

3. **No redundant semantic fields**
   - `payload_kind` is a property of the event catalog, not an independently mutable runtime field.
   - Routing class is derived from the call path (`send` versus `publish`) and the route table, not from ad-hoc message mutation.

## Minimal invariants

- Every runtime message has exactly one `event_id`.
- `event_id` must exist in `config/events.def`.
- `source_actor` must exist in `config/actors.def`.
- `target_actor == EV_ACTOR_NONE` means "publish via static route table".
- `target_actor != EV_ACTOR_NONE` means "deliver directly via `ev_send()`".
- The message header is always valid even when the payload is empty.
- A message is disposable exactly once; repeated disposal must be safe and result in a no-op after the first effective release.

## Non-goals

The canonical envelope does **not** try to encode every possible concern:

- no dynamic subscription metadata,
- no arbitrary string topic names,
- no hidden ownership transfer through undocumented conventions,
- no runtime-defined schema.

## Consequences

### Positive

- One message abstraction for the entire framework.
- Smaller API surface.
- Easier host-side testing.
- Better fit for deterministic, auditable embedded systems.

### Trade-offs

- Some rich transport features must be modeled explicitly elsewhere.
- The payload contract must stay tightly aligned with the event catalog.

## Follow-up

Implementation should provide:

- a zero-initializable `ev_msg`,
- explicit constructors for inline and leased payloads,
- `ev_msg_dispose()` as the standard cleanup path,
- static assertions linking runtime behavior to the event catalog.
