# Domain pump model

## Purpose

`ev_domain_pump_t` is the first scheduler-style layer above individual actor
runtimes.

It provides a deterministic way to cooperatively drain all actors bound to one
execution domain without introducing platform threads, timers, or RTOS
primitives.

## Why this exists

`ev_actor_runtime_pump()` solves fairness *inside one actor* by bounding how
many messages that actor can drain in one call.

A real system also needs fairness *between actors* that share one cooperative
loop.

`ev_domain_pump_t` provides that second layer.

## Core contract

A domain pump is initialized with:

- one `ev_actor_registry_t`,
- one `ev_execution_domain_t`.

At runtime it:

- inspects only actors assigned to that domain,
- skips missing bindings and empty mailboxes,
- drains each selected actor with
  `min(actor_default_budget, remaining_domain_budget)`,
- advances its internal cursor after each actor-level pump, while keeping
  the current scan base stable for the rest of that pass,
- continues until either:
  - the global budget is exhausted,
  - no more work exists in the domain,
  - an actor-level pump returns an error.

## Fairness model

The internal cursor makes the domain pump round-robin across actors.
Each scan pass snapshots the cursor at pass entry, so a cursor update never
re-bases indexes mid-pass and therefore cannot skip actors accidentally.

This prevents one actor from always being examined first when a caller invokes
small global budgets repeatedly.

Fairness is therefore layered:

1. `ev_actor_runtime_pump()` prevents one actor from monopolizing a cooperative
   loop.
2. `ev_domain_pump_run()` prevents one actor from monopolizing a logical
   execution domain.

## Return semantics

`ev_domain_pump_run()` returns:

- `EV_ERR_EMPTY` when no work was pending at entry,
- `EV_OK` when one or more messages were processed successfully,
- an actor-level error when a handler or dispose path fails,
- `EV_ERR_CONTRACT` when actor metadata violates runtime assumptions,
- `EV_ERR_INVALID_ARG` on invalid input.

When `EV_OK` is returned:

- `report.stop_result = EV_ERR_EMPTY` means the domain became idle before the
  global budget was consumed,
- `report.exhausted_budget = true` means the global budget ended while more work
  was still pending.

## Report fields

`ev_domain_pump_report_t` exposes:

- `budget`
- `processed`
- `pending_before`
- `pending_after`
- `actors_examined`
- `actors_pumped`
- `exhausted_budget`
- `last_actor`
- `stop_result`

`actors_examined` counts domain actors considered during scheduling scans.
`actors_pumped` counts actor runtimes that were actually drained.

## Non-goals

At this stage the domain pump does **not**:

- assign OS priorities,
- sleep or block,
- own timers,
- perform inter-domain scheduling,
- integrate with ESP8266 RTOS tasks.

That belongs to later deployment and platform layers.
