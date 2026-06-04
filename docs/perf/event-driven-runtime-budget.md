# Event-driven runtime budget contract

Runtime performance claims must be tied to bounded metrics, not prose.  Target or
host transcripts may emit compact markers of the form:

```text
EV_RUNTIME_METRIC max_actor_handler_us=900 max_mailbox_depth=3 dropped_events=0 backpressure_events=0 timer_deadline_misses=0 heap_allocations_hot_path=0
```

`tools/perf/parse_eventflow_runtime_metrics.py` checks those markers against the
current conservative defaults:

| Metric | Default limit |
|---|---:|
| `max_actor_handler_us` | 2500 |
| `max_mailbox_depth` | 8 |
| `dropped_events` | 0 |
| `backpressure_events` | 0 |
| `timer_deadline_misses` | 0 |
| `heap_allocations_hot_path` | 0 |

Absence of a real metrics log is `ENVIRONMENT_BLOCKED`.  The contract is designed
for the Half-Sync/Half-Async shape already used by the framework: low-level
adapter/HIL work may be synchronous and bounded, while actor-level progress is
mediated by queues, timers and state machines.
