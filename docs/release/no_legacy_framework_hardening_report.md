# No-legacy framework hardening report

Baseline archive: `esp8266-event-driven_20260430_080553.tar.gz`.

## Confirmed current state

The demo application is already migrated away from manual runtime ownership.
`apps/demo/include/ev/demo_app.h` owns `ev_runtime_graph_t graph`, timer tokens
`tick_1s_token` and `tick_100ms_token`, actor contexts and application state. It
does not own per-actor `ev_mailbox_t`, mailbox storage arrays,
`ev_actor_runtime_t`, `ev_actor_registry_t`, `ev_domain_pump_t`,
`ev_system_pump_t`, `next_tick_ms` or `next_tick_100ms_ms`.

`apps/demo/ev_demo_app.c` no longer performs the old composition root via
`ev_actor_registry_bind()`, `ev_domain_pump_init()`, `ev_system_pump_init()` or
`ev_system_pump_run()` as its primary scheduler. `tools/audit/static_contracts.py`
already enforces those migration blockers as hard failures.

## Remaining compatibility seams inspected

- `apps/demo/ev_demo_app.c` still reads framework internals: `app->graph.scheduler`,
  `app->graph.timer_service`, `app->graph.scheduler.system` and
  `app->graph.scheduler.domains`.
- `ev_demo_app_poll()` still contains the orchestration algorithm: collect ingress,
  drain scheduler, publish due timers, drain again, compute partial state and
  update demo statistics.
- `ev_demo_app_delivery()` is still passed to actor initialization for panel,
  supervisor, command, RTC, MCP23008, DS18B20 and OLED actors.
- `ev_runtime_is_quiescent_at()` exposes `pending_log_records`, but the log port
  currently has no pending hook, so the field cannot reflect buffered log state.
- route/delivery metrics and faults exist, but emission coverage is incomplete for
  every disabled-route, validation, QoS, overflow and delivery branch.

## Scope of this hardening iteration

This iteration encapsulates graph internals, introduces reusable `runtime_loop`,
turns demo polling into a thin wrapper, replaces demo delivery callback usage
with a graph-backed actor publish port, completes diagnostic emission, adds a
log pending hook to quiescence and strengthens static contracts against legacy
regression.

## Out of scope

SDK build matrix, HIL, Wemos physical-board smoke and final ESP8266 linker-map
RAM accounting are not claimed unless executed separately.
