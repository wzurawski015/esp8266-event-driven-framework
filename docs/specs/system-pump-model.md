# System pump model

## Purpose

`ev_system_pump_t` is the first scheduler layer that spans multiple execution
 domains.

It allows the framework core to rotate fairly across several cooperative domain
loops without introducing threads, RTOS tasks, or platform timing APIs.

## Why this exists

Fairness is layered in the runtime:

1. `ev_actor_runtime_pump()` bounds work inside one actor.
2. `ev_domain_pump_run()` bounds work across actors inside one domain.
3. `ev_system_pump_run()` bounds work across multiple domains.

This gives us a complete cooperative scheduling stack in pure C before any
ESP8266 deployment layer exists.

## Core contract

A system pump binds zero or more `ev_domain_pump_t` instances, keyed by
execution domain.

At runtime it:

- scans bound domains in round-robin order,
- skips unbound domains,
- skips bound domains with no pending work,
- grants one **domain turn** at a time,
- drains the selected domain using `ev_domain_pump_default_budget()`,
- advances its internal domain cursor after each successful domain turn,
- stops when either:
  - the caller's turn budget is exhausted,
  - no more pending work exists across bound domains,
  - a domain-level error occurs.

## Budget semantics

The system-pump budget is measured in **domain turns**, not in messages.

One domain turn may process multiple messages because the selected domain is
itself drained using a bounded domain quantum derived from actor SSOT budgets.

This keeps the model simple:

- actor budgets bound work inside one actor,
- domain budgets bound work inside one domain,
- system turn budgets bound how many domains can run cooperatively before the
  caller regains control.

## Return semantics

`ev_system_pump_run()` returns:

- `EV_ERR_EMPTY` when no bound domain had pending work at entry,
- `EV_OK` when one or more domain turns completed successfully,
- `EV_ERR_STATE` when work was reported but no progress was possible,
- `EV_ERR_CONTRACT` when a selected domain had an invalid default quantum,
- a propagated domain-level error when one domain pump fails.

When `EV_OK` is returned:

- `report.stop_result = EV_ERR_EMPTY` means all currently visible work was
  drained before the turn budget ended,
- `report.exhausted_turn_budget = true` means the scheduler consumed all allowed
  domain turns while work still remained somewhere in the bound set.

## Report fields

`ev_system_pump_report_t` exposes:

- `turn_budget`
- `turns_processed`
- `domains_examined`
- `domains_pumped`
- `messages_processed`
- `pending_before`
- `pending_after`
- `exhausted_turn_budget`
- `last_domain`
- `stop_result`

`domains_examined` counts bound domains seen by scheduling scans.
`domains_pumped` counts domains that actually consumed one cooperative turn.

## Non-goals

At this stage the system pump does **not**:

- create threads,
- assign OS priorities,
- sleep,
- integrate with timers,
- implement interrupt hand-off,
- know anything about ESP8266 RTOS SDK.

It is purely the platform-agnostic cooperative scheduling layer above domain
pumps.
