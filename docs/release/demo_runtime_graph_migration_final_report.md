# Demo runtime_graph migration final report

## Migration status

`apps/demo` now delegates runtime ownership to `ev_runtime_graph_t`: mailboxes, actor runtimes, actor registry and scheduler are owned by the graph. Demo code still owns actor contexts and application-specific state.

Timer ownership moved to `ev_timer_service_t` inside `ev_runtime_graph_t`; platform wait calculation uses `ev_demo_app_next_deadline_ms()` rather than legacy tick fields.

Delivery is routed through `ev_runtime_graph_publish()` for demo publications and through active-route checked compatibility delivery for legacy actor contexts. Disabled route counters remain compatible but are derived from active route state.

Sleep decisions use `ev_runtime_is_quiescent_at()`, graph next-deadline data and actor quiescence callbacks for OLED and DS18B20 blockers.

## Hard static contracts

`tools/audit/static_contracts.py` now treats return of demo-owned runtime primitives as a hard failure: per-actor mailboxes, per-actor actor runtimes, actor registry, domain/system pumps and legacy tick fields are forbidden in `apps/demo/include/ev/demo_app.h`; manual registry/pump initialization and direct system-pump execution are forbidden in `apps/demo/ev_demo_app.c`; ESP8266 adapter reads of old tick fields are forbidden.

## Remaining post-migration work

1. SDK build matrix for all targets.
2. HIL for ATNEL I2C / OneWire / WiFi.
3. Wemos minimal runtime smoke on a real board.
4. Full fault/metrics runtime emission for every route/delivery branch.
5. Log flush pending hook in quiescence.
6. Final memory budget from ESP8266 linker map.
7. ADR wording review after hardware validation.
