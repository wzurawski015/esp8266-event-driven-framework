# Performance baseline report

## Scope

This release note records the first report-only host microbenchmark layer for the
framework hot path. The patch adds measurement infrastructure only. It does not
change runtime semantics, SDK/HIL targets, BSP wiring, actor placement, or product
features.

## Baseline state observed before the patch

The current archive already has a strong structural foundation:

- generated route table: 53 routes;
- generated exact mailbox layout: 17 actors and 144 mailbox slots;
- active-route spans are present and checked;
- route QoS behavior is centralized in the delivery service;
- graph internal field access is already denied outside the runtime boundary;
- host, property, static-contract, memory-budget, and SDK matrix checks pass.

## Added measurement surface

The patch introduces:

- `tests/bench/bench_runtime_publish.c` for static publish, active publish,
  fanout, single-target, and publish-report measurements;
- `tests/bench/bench_runtime_poll.c` for direct runtime poll and public
  runtime-loop poll measurements;
- `tests/bench/bench_support.h` for a small shared host timing/report contract;
- `tools/bench_report.py` for schema validation of benchmark output;
- `make bench` and `make perf-gate` targets;
- this report and the updated performance-baseline specification.

## Gate posture

`perf-gate` is deliberately report-only. It fails when benchmark binaries fail,
when a required benchmark line is missing, or when the output schema is invalid.
It does not fail on absolute timing values.

This is the correct first step because fixed timing budgets without variance data
would create a false sense of precision and would make CI sensitive to host noise.

## Current technical exceptions

| Exception | Status | Impact on this patch |
|---|---|---|
| Device actors still live under `core/src` | Known temporary architecture exception | No movement attempted. |
| `ev_runtime_graph_t` is still public | Known temporary static-storage exception | Benchmarks use public APIs and avoid direct graph-field access. |
| Delivery trace uses `timestamp_us = 0U` | Known trace exception | Host benchmark timing is independent of runtime trace timestamps. |
| Wemos `board_secrets.local.h` is deliberately tracked | Private lab exception | File is preserved and untouched; no new secrets are introduced. |

## Enforcement path

Recommended next steps:

1. Run `make bench` on at least three comparable host sessions and archive
   `build/bench/results.txt` outputs.
2. Add a non-failing trend report before any hard thresholds.
3. Add conservative warning budgets after variance is understood.
4. Only then promote selected metrics into hard release gates.
5. Continue with `sdk: add stack-usage and map-budget release gates` or
   `trace: timestamp delivery records from monotonic clock port`, depending on
   whether release evidence or observability is the next priority.

## Validation commands

The patch is expected to pass:

```sh
git diff --check
python3 tools/routegen/routegen.py
python3 tools/routegen/mailbox_layoutgen.py --check
python3 tools/audit/routegen_check.py
python3 tools/audit/static_contracts.py
python3 tools/audit/actor_module_descriptor_consistency.py
python3 tools/audit/memory_budget.py
./tools/fw sdk-matrix-check
make host-test
make property-test
make quality-gate
make bench
make perf-gate
```
