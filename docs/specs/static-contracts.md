# Static contracts

`tools/audit/static_contracts.py` checks:

- no heap APIs in forbidden layers,
- no `portMAX_DELAY` or actor/runtime `vTaskDelay`,
- no ESP8266 SDK include leakage into portable layers,
- generated route count freshness,
- fault and metrics catalog presence,
- BSP profile presence,
- pins schema,
- production source markers.

Run:

```sh
make static-contracts
```

## Migration blocker contract status

The preparation-era `MIGRATION_BLOCKER_REPORTED` mode is no longer the release posture.
After the demo migration and no-legacy hardening, the static-contract checker
hard-fails reintroduction of demo-owned runtime primitives, direct demo access to
`runtime_graph` scheduler/timer internals, and production actor initialization
through the old demo delivery callback.

## Demo runtime ownership contract

The static-contract checker fails if demo code reintroduces per-actor mailboxes, per-actor actor runtimes, actor registry ownership, domain/system pump ownership, legacy tick fields or adapter reads of those legacy fields. Compatibility wrappers are allowed only when they delegate to `runtime_graph`.


## No-legacy demo contracts

The static audit hard-fails reintroduction of demo-owned runtime primitives and, after no-legacy hardening, also hard-fails direct demo access to graph scheduler and timer internals or production actor initialization through `ev_demo_app_delivery`.


## FreeRTOS/vendor heap APIs

The heap deny-list covers standard C allocation APIs and FreeRTOS/vendor spellings: `pvPortMalloc`, `vPortFree`, `heap_caps_malloc`, and `heap_caps_free`. The scanner strips C/C++ comments before matching and scans host/property tests in addition to portable framework layers.
