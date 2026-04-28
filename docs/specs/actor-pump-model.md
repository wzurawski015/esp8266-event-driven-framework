# Actor pump model

## Purpose

`ev_actor_runtime_pump()` introduces a bounded-drain primitive for cooperative
execution domains.

The goal is simple: one actor may own a queue, but it must not monopolize a
fast loop forever just because it currently has backlog. Work is therefore
processed in bounded slices.

## Why this exists

A single-message `step()` is easy to reason about but often too fine-grained for
high-throughput paths. An unbounded `drain-until-empty` loop is efficient in
isolation but dangerous in a cooperative scheduler because one hot actor can
starve the rest of the system.

The bounded pump is the middle ground:

- more efficient than one-message polling,
- still deterministic,
- fairer than draining until empty.

## Contract

A pump run is controlled by a positive `budget`.

`ev_actor_runtime_pump(runtime, budget, report)` stops when one of these happens:

1. the queue becomes empty before the budget is consumed,
2. the budget is consumed,
3. handler or dispose reports an error.

If the runtime had no pending work at call entry, the function returns
`EV_ERR_EMPTY` and records an empty-step event.

If some work was processed and the queue became empty before the budget was
consumed, the function returns `EV_OK` and reports `stop_result = EV_ERR_EMPTY`.

If the budget is consumed while messages are still pending, the function returns
`EV_OK` and reports `exhausted_budget = true`.

## Default budget from SSOT

The default per-actor budget lives in `config/actors.def`.

That means bounded-drain policy is not hidden in scheduler code; it is part of
actor metadata and therefore part of the framework contract.

Current initial policy:

- `ACT_STREAM` gets the largest budget,
- `ACT_BOOT` and `ACT_APP` get moderate budgets,
- `ACT_DIAG` gets a smaller budget because it belongs to a slower execution
  domain and should not dominate a cooperative loop.

## Diagnostics

Each runtime tracks bounded-drain counters:

- `pump_calls`
- `pump_budget_hits`
- `last_pump_budget`
- `last_pump_processed`

These are deliberately small deterministic counters. They are enough to reason
about fairness and budget pressure before introducing platform-specific timing.

## Non-goals

This stage does not yet add:

- a global scheduler across many runtimes,
- time-sliced wall-clock budgets,
- priority queues,
- RTOS integration.

Those belong to later runtime layers. The current contract only establishes the
bounded-drain primitive and its semantics.
