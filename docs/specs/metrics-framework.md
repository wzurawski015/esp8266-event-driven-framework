# Metrics framework

Metrics are defined in `config/metrics.def` and stored in a static `ev_metric_registry_t`.

Public API:

```c
void ev_metric_registry_init(ev_metric_registry_t *registry);
ev_result_t ev_metric_sample(...);
ev_result_t ev_metric_increment(...);
ev_result_t ev_metric_set_gauge(...);
ev_result_t ev_metric_snapshot(...);
```

Metrics cover publish/post results, delivery results, mailbox pressure, timer events, quiescence decisions, sleep decisions, faults, trace drops, network backpressure, and command security outcomes.
