# Performance regression budget policy

`perf-gate` is a hard regression gate. `perf-report` remains available for report-only diagnosis. Budgets live in `config/perf_budgets.json` and are intentionally conservative to tolerate host noise while catching large regressions.
