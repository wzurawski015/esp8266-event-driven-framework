# Performance Baseline

This specification defines the first host-side performance baseline for the
framework hot path. It is intentionally **measurement-only** in this iteration:
`make bench` and `make perf-gate` validate that every required benchmark runs and
emits machine-readable numbers, but they do not enforce fixed nanosecond limits.
Hard thresholds should be introduced only after several comparable host runs are
recorded on a stable reference machine.

## Non-negotiable quality gates

1. **ISR execution time:** GPIO ISR handlers must execute in bounded time and must
   not loop over unconfigured lines.
2. **Bounded execution:** the application/runtime poll path must respect drain
   budgets so that one actor or domain cannot monopolize the cooperative loop.
3. **Zero-heap policy:** dynamic allocation is forbidden after bootstrap. Runtime
   queues and mailboxes are statically allocated, and adapter hot paths must not
   rely on hidden SDK heap allocation.
4. **Deep-sleep entry:** the system must flush asynchronous log and platform
   buffers before entering deep sleep.
5. **Hot-path visibility:** publish and poll costs must be measurable on the host
   before architectural refactors are allowed to claim performance neutrality.

## Host microbenchmark contract

The host benchmark suite lives under `tests/bench/` and is invoked through:

```sh
make bench
make perf-gate
```

`make bench` builds framework objects with `BENCH_CFLAGS`, runs each benchmark
binary, stores raw output in `build/bench/results.txt`, and validates the output
with `tools/bench_report.py`. `make perf-gate` is currently a report-only alias
that depends on `bench`.

Each benchmark emits one line in this format:

```text
BENCH <name> iterations=<n> logical_ops=<n> total_ns=<n> ns_per_iter=<n> ns_per_op=<n> checksum=<n>
```

The checksum is not a correctness oracle; it prevents the benchmark body from
collapsing into an unused loop and gives the report parser a deterministic field
to validate.

## Required benchmark set

The first baseline covers these hot paths:

| Benchmark | What it measures | Notes |
|---|---|---|
| `static_publish_tick_fanout` | Static route-table publish for a high-fanout event | Uses `EV_TICK_1S`. |
| `static_publish_with_report` | Static publish with `ev_publish_report_t` accounting | Uses the same fanout event. |
| `active_publish_tick_fanout` | Active-route publish for a high-fanout event | Uses a fully built host graph and resets target mailboxes between timed batches. |
| `active_publish_fault_single` | Active-route publish for a single-target event | Uses `EV_FAULT_REPORTED`. |
| `active_publish_with_delivery_report` | Active-route publish with `ev_delivery_report_t` accounting | Uses the single-target fault route. |
| `runtime_poll_empty` | Direct runtime poll with no pending work | Measures empty scheduler/timer overhead. |
| `runtime_poll_prefilled_mailbox` | Direct runtime poll with actor mailbox work | Times the poll call only; mailbox fill is outside the timed region. |
| `runtime_loop_poll_empty` | Public runtime-loop poll with no pending work | Uses the public `ev_runtime_loop_poll_once` entrypoint. |
| `runtime_loop_poll_prefilled_mailbox` | Public runtime-loop poll with actor mailbox work | Times the loop poll call only; mailbox fill is outside the timed region. |

## Interpretation rules

- A single host run is evidence, not a contract.
- Compare results only across the same compiler, optimization flags, CPU governor,
  and host load profile.
- The first release gate must remain report-only. A later commit may add warning
  budgets, and a still later commit may add hard budgets after variance is known.
- Benchmark code must not introduce runtime semantics, SDK/HIL behavior, or new
  application features.

## Current exceptions and caveats

- `ev_runtime_graph_t` is still public for static storage ownership. Benchmarks use
  public graph APIs and public actor-runtime/mailbox APIs, but this should be
  revisited when the graph becomes fully opaque.
- Concrete device actors still live in `core/src`; this benchmark patch measures
  the current layout rather than moving it.
- Delivery trace timestamps still use `timestamp_us = 0U`; timing reported here is
  host-side benchmark timing, not runtime trace timing.
- The Wemos ESP-WROOM-02 18650 BSP includes a private, deliberately tracked
  `board_secrets.local.h` in this lab repository. The benchmark patch does not
  modify it and does not add new secrets.
