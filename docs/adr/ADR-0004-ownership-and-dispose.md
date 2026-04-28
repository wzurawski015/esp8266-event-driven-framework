# ADR-0004: Ownership and dispose contract

- Status: Accepted
- Date: 2026-03-14

## Context

The framework must support high-performance message passing without runtime heap allocation in hot paths.
That requires explicit lifetime management and a cleanup rule that is simple enough to apply everywhere.

## Decision

We standardize on a single cleanup idiom:

- every received message must eventually go through `ev_msg_dispose()`,
- disposal is mandatory even for paths that terminate early,
- for payload kinds that do not own external resources, disposal is a safe no-op,
- for leased or stream-backed resources, disposal performs the required release action.

## Ownership classes

### Inline

- Data lives entirely inside the message envelope.
- No external release is needed.
- `ev_msg_dispose()` is a no-op after validation/reset.

### Lease

- The message references a buffer or object owned by a pool.
- Delivery may increase the number of outstanding references.
- Queueing or publish fan-out must acquire retained shares explicitly.
- Pool-backed leases expose that ownership through explicit handles and deterministic slot refcounts.
- Disposal decrements the reference count and returns the resource to the pool when it reaches zero.

### View

- The message carries a read-only non-owning view.
- The backing storage lifetime must be guaranteed by a separate owner.
- Views must never be used to smuggle mutable shared state.

### Stream

- The message describes a chunk, slice, or cursor over streaming storage.
- Disposal releases the stream reservation or cursor ownership as defined by the stream backend.

## Core rules

- Ownership must be explicit in constructors and API names.
- Consumers must not assume that a lease or stream survives past disposal.
- Consumers must not retain raw pointers after disposal.
- Mutation is allowed only by the current owner.
- Shared readable access must be represented as lease or view semantics, never by undocumented aliasing.

## Failure-path rule

If any step after message creation fails, the creator or the current owner must dispose the message before returning the error.
There must be no "best effort" cleanup conventions.

## Consequences

### Positive

- One universal cleanup path.
- Easier code review for memory safety.
- Better fit for host-side leak detection and pool accounting.

### Trade-offs

- Slightly more boilerplate at message boundaries.
- The API must make partial initialization and moved-from states well-defined.

## Follow-up

Implementation should provide:

- message constructors with explicit ownership names,
- `ev_msg_dispose()` that is safe on zero-initialized messages,
- a deterministic lease pool with explicit retain/release accounting,
- host-side tests for normal and failure-path cleanup,
- pool diagnostics for lease and stream pressure.

## Initialization safety

`ev_msg_init_publish()` and `ev_msg_init_send()` must be safe on first use with
arbitrary stack storage. They therefore perform a blind reset before assigning
event and actor fields and must not inspect `msg->cookie` or any payload field
from the previous bytes.

This removes a dangerous hidden requirement that callers zero-initialize every
transient `ev_msg_t` before first use. Reuse is intentionally explicit: callers
that may hold an attached payload must call `ev_msg_dispose()` before the next
initialization. Constructors do not release old payloads implicitly.
