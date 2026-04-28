# Event Model

## Principles

- Events exist only when declared in `config/events.def`.
- Actors exist only when declared in `config/actors.def`.
- Routes exist only when declared in `config/routes.def`.
- Routing is deterministic and reviewable.

## Why static routing

Blind broadcast pub/sub is easy to write and hard to control in constrained embedded systems.
Static routing is preferable here because it gives:

- bounded fan-out,
- better auditability,
- clearer ownership,
- simpler performance analysis,
- more reliable memory accounting.

## Planned mailbox semantics

Mailbox semantics should be attached to actor inboxes or routes, not treated as vague event-level wishes.

Candidate forms:

- `FIFO_N`
- `MAILBOX_1`
- `LOSSY_RING_N`
- `COALESCED_FLAG`
- `REQRESP_SLOT`

## Publish failure semantics

`ev_publish()` is the fail-fast convenience wrapper.
It stops on the first delivery error and returns that error.

`ev_publish_ex()` adds explicit policy:

- `EV_PUBLISH_FAIL_FAST` stops on the first failed route.
- `EV_PUBLISH_BEST_EFFORT` attempts every matching route.

Best-effort publish returns:

- `EV_OK` when every matched route succeeds,
- `EV_ERR_PARTIAL` when at least one route succeeds and at least one fails,
- the first delivery error when every matched route fails,
- `EV_ERR_NOT_FOUND` when no static route matches the event.

The accompanying `ev_publish_report_t` is the canonical source for fan-out accounting.
