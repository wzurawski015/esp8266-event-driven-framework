# Timer service

The timer service provides fixed-slot one-shot and periodic deadlines. It uses `uint32_t` monotonic millisecond time and wrap-safe signed-delta comparison.

Public API:

```c
void ev_timer_service_init(ev_timer_service_t *svc);
ev_result_t ev_timer_schedule_oneshot(...);
ev_result_t ev_timer_schedule_periodic(...);
ev_result_t ev_timer_cancel(...);
uint32_t ev_timer_publish_due(...);
ev_result_t ev_timer_next_deadline_ms(...);
size_t ev_timer_pending_count(...);
uint8_t ev_timer_is_due(...);
```

The service does not allocate memory. `publish_due` is bounded by the caller-provided budget and can therefore be used inside deterministic polling loops and sleep-deadline calculations.
