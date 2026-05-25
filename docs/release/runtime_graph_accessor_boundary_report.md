# Runtime graph accessor boundary report

Date: 2026-05-25
Scope: non-functional runtime graph boundary hardening. No SDK/HIL behavior, Wi-Fi
behavior, route semantics, actor placement, or application feature semantics are
changed by this patch.

## Input state

The analysed archive was `esp8266-event-driven-framework_20260525_103046.tar.gz`.
Its SHA-256 was:

```text
cc3b366145abb30816351b696b7ccf5476058a387189e1590567893cdaea8914
```

The current code already has a hard layering contract, actor/module descriptor
consistency checks, generated exact mailbox storage layout, and active route
spans. The runtime graph still owns the canonical scheduler, timer service,
mailboxes, metrics, trace ring, delivery service, ports, and board profile.

## Observed issues before this patch

- Several host tests inspected `ev_runtime_graph_t` fields directly:
  `scheduler`, `timer_service`, `mailboxes`, `mailbox_storage`, `metrics`,
  `trace_ring`, and `ports`. The memory-budget probe also inspected
  `mailbox_storage` through a null-cast `sizeof` expression.
- `static_contracts.py` used recursive `Path.rglob()` filtering after traversal,
  so an extracted archive containing ignored directories such as `build/` could
  make the audit expensive or hang before reaching the actual contract checks.
- `ev_runtime_graph_t` is still a public structure because applications and
  targets currently embed it as static storage.

## Changes introduced

- Added public graph accessors for the existing external white-box needs:
  mailbox storage sizing, per-actor mailbox offset/capacity, metric reads,
  system-pump bound count, scheduler poll count, one-shot timer scheduling,
  timer pending count, and trace record/clear operations.
- Updated host tests and the memory-budget probe to avoid direct graph-field
  reads outside the runtime boundary.
- Extended `tools/audit/static_contracts.py` with a runtime-graph field-access
  audit. The audit rejects direct graph internal field access outside
  `runtime/src/` and `runtime/include/ev/runtime_graph.h`; embedded C probes in
  Python audit tools are scanned as well.
- Changed the static-contract filesystem walk to prune ignored directories before
  descent, keeping the quality gate stable in dirty or extracted workspaces.
- Updated static-contract and layering documentation to describe the new boundary.

## Current retained exceptions

- `ev_runtime_graph_t` remains a public struct for static storage ownership. This
  is a deliberate intermediate state, not the final architecture.
- Runtime internals may still access graph fields inside `runtime/src/`.
- `runtime/include/ev/runtime_graph.h` still contains the struct definition.
- Concrete device actors are still in `core/src`.
- Delivery trace records still use `timestamp_us = 0U`.
- The Wemos ESP-WROOM-02 18650 BSP has a private, deliberately tracked
  `board_secrets.local.h`. This is a private lab exception to normal secret
  hygiene and must not be generalized.

## Enforcement model

The new audit is intentionally conservative. It does not ban owning an
`ev_runtime_graph_t` object and does not force a full opaque-type refactor. It
rejects direct field access patterns in C/H files and embedded probe strings in
Python audit tools, such as:

```text
graph.scheduler
graph->timer_service
app->graph.metrics
((ev_runtime_graph_t *)0)->mailbox_storage
```

This keeps the patch mechanical and reviewable while preventing future external
white-box coupling.

## Recommended next patch

The next most coherent step is:

```text
qos: enforce route delivery policies end to end
```

A full opaque `ev_runtime_graph_t` can follow later, after the static-storage
contract is formalized. Moving device actors out of `core/` should wait until the
route QoS and runtime access boundaries are stable.
