# Static contracts

`tools/audit/static_contracts.py` checks repository-wide invariants that are safe
to enforce without changing runtime semantics.

Run:

```sh
make static-contracts
```

## Rules already enforced

- No heap APIs in forbidden portable/test layers.
- No `portMAX_DELAY` or actor/runtime `vTaskDelay` in portable runtime layers.
- No ESP8266 SDK include leakage into portable layers.
- Generated route count freshness against `config/routes.def`.
- Fault, metrics, module, and capability SSOT file presence.
- BSP profile presence and `pins.def` schema.
- No production `TODO`/`FIXME` markers in scanned C/H sources.
- No patch conflict artifacts such as `.orig` or `.rej`.
- Adapter bootstrap/static primitives are allowlisted and stale allowlist rows fail.
- Demo runtime migration blockers do not reappear: demo-owned mailbox/runtime
  storage, direct scheduler/timer graph access, legacy demo tick fields, and old
  delivery-callback actor initialization remain forbidden.
- Direct access to `ev_runtime_graph_t` internal fields is forbidden outside the
  runtime boundary. The type may still be embedded by applications for static
  storage, but fields such as `scheduler`, `timer_service`, `mailboxes`,
  `mailbox_storage`, `metrics`, `trace_ring`, and `active_routes` must be reached
  through public graph accessors. Embedded C probes in Python audit tools are
  included in this boundary check.
- Repository scans prune ignored generated/build/log/docker/git directories
  before walking, so quality gates do not depend on a clean workspace.
- Actor/module descriptor consistency is checked by the dedicated
  `tools/audit/actor_module_descriptor_consistency.py` gate.
- Exact mailbox layout freshness is checked by `tools/routegen/mailbox_layoutgen.py
  --check`.
- Route delivery QoS failure behavior is centralized in the delivery-service
  policy API and covered by focused host tests. The static audit also rejects
  reintroduction of the old hand-coded strict QoS disjunction in the delivery
  service.
- The hard layering contract document exists and preserves the main layer
  sections: config/codegen, core kernel, runtime, actor descriptors/module
  registry, device actors, drivers, ports, apps, adapters, bsp, tests, tools, and
  docs.

## Rules planned but not yet enforced

- `ev_runtime_graph_t` should become fully opaque after the static-storage model
  has an explicit public allocation/size contract.
- Runtime graph access should eventually move from field-token denial to a
  stronger positive accessor-usage model.
- Device actors should move out of `core/` after graph accessors and route policy
  contracts are hardened.
- Delivery trace records should timestamp events from the monotonic clock port.
- `route_policy_flags` should be renamed or split after the route policy model is
  migrated from a single historical class field to a true policy descriptor.
- SDK release memory checks now include app-bin matrix budgeting and a report-only
  `.su` stack max-frame baseline; a future gate may add a dedicated stack budget
  field once `.su` generation is standardized across SDK targets.

## Known temporary exceptions

- Concrete device actors are still in `core/src` and their headers still live in
  `core/include/ev`.
- `ev_runtime_graph_t` is still public for static storage ownership, but direct
  external field access is now rejected by `tools/audit/static_contracts.py`.
- Delivery trace currently sets `timestamp_us = 0U`.
- `route_policy_flags` is still a historical name: it behaves like a single
  accepted route QoS class with compatibility allowances, not a true bitset.
- The Wemos ESP-WROOM-02 18650 BSP has a private, deliberately tracked
  `board_secrets.local.h`. This is a private lab exception to normal secret
  hygiene and must not be generalized.

## Migration blocker contract status

The preparation-era `MIGRATION_BLOCKER_REPORTED` mode is no longer the release
posture. After the demo migration and no-legacy hardening, the static-contract
checker hard-fails reintroduction of demo-owned runtime primitives, direct demo
access to `runtime_graph` scheduler/timer internals, and production actor
initialization through the old demo delivery callback.

## Demo runtime ownership contract

The static-contract checker fails if demo code reintroduces per-actor mailboxes,
per-actor actor runtimes, actor registry ownership, domain/system pump ownership,
legacy tick fields, or adapter reads of those legacy fields. Compatibility
wrappers are allowed only when they delegate to `runtime_graph`.

## No-legacy demo contracts

The static audit hard-fails reintroduction of demo-owned runtime primitives and,
after no-legacy hardening, also hard-fails direct demo access to graph scheduler
and timer internals or production actor initialization through
`ev_demo_app_delivery`.

## FreeRTOS/vendor heap APIs

The heap deny-list covers standard C allocation APIs and FreeRTOS/vendor
spellings: `pvPortMalloc`, `vPortFree`, `heap_caps_malloc`, and `heap_caps_free`.
The scanner strips C/C++ comments before matching and scans host/property tests
in addition to portable framework layers.
