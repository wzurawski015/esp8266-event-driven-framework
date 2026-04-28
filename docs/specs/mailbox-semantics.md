# Mailbox Semantics

## Purpose

Mailbox semantics define what happens at the boundary between delivery and actor execution.
They are properties of inboxes and delivery paths, not vague wishes attached to events.

## Canonical forms

### `FIFO_N`

- Bounded first-in, first-out queue.
- Preserves insertion order per mailbox.
- Best default for commands and most control-plane traffic.

### `MAILBOX_1`

- Single-slot mailbox.
- New delivery fails or replaces according to explicit policy.
- Useful for state snapshots or latest-only control inputs.

### `LOSSY_RING_N`

- Bounded queue optimized for streaming or telemetry.
- Overflow policy is explicit and measurable.
- Suitable only where loss is acceptable by design.

### `COALESCED_FLAG`

- Repeated events collapse into one pending indication.
- Useful for "work available" notifications.
- Payload must either be absent or externally recoverable from owned state.

### `REQRESP_SLOT`

- Dedicated handshake slot for tightly controlled request/response exchanges.
- Intended for very small control protocols where overlap must stay bounded.

## Overflow policy

Overflow behavior must never be implicit.
Every mailbox kind must define one of the following behaviors explicitly:

- reject new delivery,
- overwrite existing content,
- drop oldest,
- drop newest,
- coalesce.

## Design rules

- Mailbox semantics belong to the receiving actor or route, not to a global bus.
- The semantic name must reveal queueing behavior.
- Capacity must be explicit and reviewable.
- Streaming paths should use lossy structures only when the data contract permits loss.
- Safety-critical or stateful control paths should prefer reject-on-overflow semantics.

## Current contract-stage mapping

The current host-verified runtime maps the bootstrap mailbox names to explicit behavior:

- `EV_MAILBOX_FIFO_8` — ordered FIFO with capacity 8 and reject-on-overflow.
- `EV_MAILBOX_FIFO_16` — ordered FIFO with capacity 16 and reject-on-overflow.
- `EV_MAILBOX_MAILBOX_1` — single-slot mailbox with overwrite-on-new-delivery.
- `EV_MAILBOX_LOSSY_RING_8` — capacity 8 with drop-oldest-on-overflow.
- `EV_MAILBOX_COALESCED_FLAG` — single pending indication that coalesces repeated payloadless deliveries of the same event.

At this stage the concrete mailbox implementation stores runtime envelopes by value. Messages carrying retainable LEASE payloads are queue-safe: mailbox enqueue acquires one additional ownership share for the queued copy, and mailbox drop/reset paths release queued shares deterministically. STREAM_VIEW payloads remain unsupported for mailbox enqueue until the stream backend contract is introduced.

## Planned normalization

The scaffold currently uses names such as `EV_MAILBOX_FIFO_8` and `EV_MAILBOX_FIFO_16`.
This is acceptable as a bootstrap shorthand, but the long-term SSOT should separate:

- mailbox kind, and
- mailbox depth.

That split improves code generation, diagnostics, and formal review.
