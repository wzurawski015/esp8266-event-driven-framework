# Performance regression budget report

## Summary

This patch converts `perf-gate` from report-only into hard regression-budget enforcement.

## Files changed

- `config/perf_budgets.json` defines the required benchmark set and hard/warning limits.
- `tools/bench_report.py` validates benchmark output, budget schema, required/duplicate metrics, and hard budget violations.
- `Makefile` splits `perf-report`, `perf-budget-gate`, and `perf-gate`.
- `docs/specs/performance_baseline.md` defines the required benchmark contract.
- `docs/release/performance_baseline_report.md` documents the promoted gate posture.

## Gate behavior

`perf-report` remains diagnostic. `perf-budget-gate` and `perf-gate` enforce hard budgets. Report-only mode cannot hide a hard-budget failure in the release gate.

## Validation

Expected validation includes `python3 tools/bench_report.py --self-test`, `make perf-report`, `make perf-budget-gate`, and `make perf-gate`.

## Scope exclusions

No runtime semantics, power protocol, QoS semantics, mailbox layout, route table semantics, actor placement, SDK/HIL behavior, or zero-copy payload contract are changed by this patch.
