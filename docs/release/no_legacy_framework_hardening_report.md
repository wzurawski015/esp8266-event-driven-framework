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

## Commit 2: graph API encapsulation

Public `runtime_graph` wrappers now provide timer scheduling, due-timer
publication, scheduler polling, pending counts and pump statistics. `apps/demo`
uses those wrappers instead of direct `graph.scheduler` or `graph.timer_service`
access for its remaining compatibility paths.

## Commit 3: reusable runtime_loop

`runtime_loop` now owns bounded orchestration of ingress collection, scheduler
draining, due-timer publication and partial-work reporting. It is a runtime-layer
service and has no dependency on `apps/demo`.

## Commit 4: demo poll wrapper

`ev_demo_app_poll()` now prepares runtime-loop policy/ports, delegates to
`ev_runtime_loop_poll_once()` and records compatibility statistics from the
runtime-loop report. Demo-specific ingress remains a callback; the orchestration
algorithm is framework-owned.

## Commit 5: actor publish port

A graph-backed `ev_actor_publish_port_t` now adapts the existing `ev_delivery_fn_t`
actor API to `runtime_graph` send/publish semantics. It preserves optional
disabled-route behavior and keeps actor emission independent of `ev_demo_app_t`.

## Commit 6: demo delivery callback removed from actor initialization

Demo actor initialization now passes `ev_actor_publish_port_delivery_adapter` with
actor-specific graph-backed publish ports. `ev_demo_app_delivery()` is no longer
the production actor emission path. Disabled-route compatibility counters are
synchronized from publish-port statistics.

## Commit 7: route/delivery diagnostics

Route binding and delivery now increment concrete metrics and emit faults for
validation rejection, QoS conflicts, active-route overflow, optional-disabled
routes and critical delivery failures. Tests assert representative metric values
for optional disabled routes and successful graph publication.

## Commit 8: log pending quiescence and hard contracts

`ev_log_port_t` now has an optional `pending` hook.
`ev_runtime_is_quiescent_at()` uses it to populate `pending_log_records` and to
apply `EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED` log policy. Static contracts
hard-fail direct demo access to graph scheduler/timer internals, direct scheduler
or timer calls from `ev_demo_app_poll()`, and use of `ev_demo_app_delivery` as an
actor emission path.

## Remaining post-hardening work

1. SDK build matrix for all targets.
2. HIL for ATNEL I2C / OneWire / WiFi.
3. Wemos minimal runtime smoke on a real board.
4. Final memory budget from ESP8266 linker map.
5. Release report with real SDK/HIL results.

## Validation limitation in this environment

`make docgen PYTHON=/usr/bin/python3` was executed successfully.
`make docs PYTHON=/usr/bin/python3` and therefore `make release-gate` could not be
completed in this container because the `doxygen` executable is not installed.
This is an environmental limitation, not a host-side compile/test failure.
SDK and HIL validation were not executed in this environment and are not claimed.
