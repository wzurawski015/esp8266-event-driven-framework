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
