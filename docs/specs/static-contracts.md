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

## Migration blocker reporting

The static contract checker reports remaining demo-runtime migration blockers as
`MIGRATION_BLOCKER_REPORTED`. These are non-failing during the preparation
iteration because full demo migration is intentionally deferred.
