# Performance baseline report

This report records the current host microbenchmark surface and its promotion from a purely report-only baseline to a regression-budgeted gate.

## Current gate posture

| Target | Meaning |
|---|---|
| `make perf-report` | Runs benchmarks and validates output schema in report-only mode. |
| `make perf-budget-gate` | Runs benchmarks and enforces `config/perf_budgets.json`. |
| `make perf-gate` | Hard gate alias for `perf-budget-gate`. |

`perf-gate` is no longer report-only. It fails on missing benchmarks, malformed output, invalid budget schema, duplicate metrics, and hard budget violations.

## Required benchmarks

The current budget file covers nine required measurements: static publish, active publish, report bookkeeping, direct runtime poll, and public runtime loop poll. These are the hot-path surfaces most sensitive to accidental route scanning, mailbox overhead, report bookkeeping overhead, or wrapper regressions.

## Budget philosophy

The thresholds are conservative host regression budgets. They are not marketed as ESP8266 hardware latency claims. The limits are selected to let the current code pass with normal host noise while catching large accidental slowdowns. Timing-budget refreshes require a release note with evidence; silently loosening limits is not acceptable.

## Non-goals

This report does not change runtime semantics, power-state behavior, QoS semantics, mailbox layout, actor placement, SDK/HIL targets, or the zero-allocation hotpath contract.
