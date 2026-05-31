# Performance baseline specification

The ESP8266 framework keeps two separate performance modes:

- `perf-report`: diagnostic/report-only benchmark output for local trend work;
- `perf-gate`: hard regression-budget enforcement using `config/perf_budgets.json`.

## Required benchmarks

The budget file and benchmark output must contain exactly these required metrics:

| Benchmark | Scope |
|---|---|
| `static_publish_tick_fanout` | Static route fanout publish hot path. |
| `static_publish_with_report` | Static publish with delivery report bookkeeping. |
| `active_publish_tick_fanout` | Active route span publish hot path. |
| `active_publish_fault_single` | Fault event active delivery path. |
| `active_publish_with_delivery_report` | Active publish with report bookkeeping. |
| `runtime_poll_empty` | Runtime poll when no work is ready. |
| `runtime_poll_prefilled_mailbox` | Runtime poll through queued actor messages. |
| `runtime_loop_poll_empty` | Public runtime loop wrapper, no work ready. |
| `runtime_loop_poll_prefilled_mailbox` | Public runtime loop wrapper with queued actor messages. |

## Budget policy

Budgets are intentionally conservative host-side regression limits, not absolute claims about ESP8266 cycle counts. A budget must be tight enough to catch large regressions but loose enough to avoid random failures from host scheduler noise. Refreshing a budget is allowed only with a new baseline report explaining the hardware, compiler, command, old value, new value, and reason.

## Failure conditions

`perf-gate` must fail if a required benchmark is missing, duplicated, malformed, has non-positive iteration/timing fields, violates a hard budget, or if the budget file is missing/inconsistent with the required benchmark set. `perf-report` may print diagnostics without enforcing timing budgets.
