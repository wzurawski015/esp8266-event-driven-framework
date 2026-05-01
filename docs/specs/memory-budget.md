# Memory budget

`tools/audit/memory_budget.py` compiles a host-side size probe and writes `docs/release/memory_budget_report.md`.

The probe measures representative static structures such as:

- runtime graph,
- message,
- mailbox,
- timer service,
- ingress service,
- fault registry,
- metrics registry,
- trace ring,
- network outbox.

Run:

```sh
make memory-budget
```

## Distinction from ESP8266 linker-map budget

`make memory-budget` checks host-side static object budgets. ESP8266 production
release memory status is reported by `tools/sdk_memory_matrix.py` from SDK ELF
section markers and per-target thresholds.
