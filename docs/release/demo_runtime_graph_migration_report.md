# Demo runtime_graph migration report

## Baseline confirmation

The baseline archive `esp8266-event-driven_20260429_225838.tar.gz` has the runtime_graph preparation from the previous iteration: actor instance descriptors, active route table, graph publish/send APIs, runtime scheduler, time-aware quiescence, sequence rings and `release-gate` are present.

The demo application still owns the compatibility runtime before this migration: manual tick fields, per-actor mailboxes, mailbox storage arrays, actor runtimes, actor registry, domain pumps and system pump in `apps/demo/include/ev/demo_app.h`; manual mailbox/runtime/registry/pump/timer/delivery/sleep-guard logic in `apps/demo/ev_demo_app.c`; and the ESP8266 runtime adapter still enters through `ev_demo_app_poll()`.

## Baseline golden tests

This migration starts by freezing boot, disabled-route, tick-order, sleep-guard and fairness behavior with host golden tests. Later commits may update the implementation, but these externally observable behaviors must remain compatible.

## Commit 3 runtime ownership migration

Demo actor contexts remain application-owned, but mailbox storage, actor runtimes, registry and scheduler ownership move to `ev_runtime_graph_t graph`. The public demo API remains as compatibility wrappers.

## Commit 4 timer migration

Demo periodic ticks are now scheduled through `ev_timer_service_t` in `ev_runtime_graph_t`. The ESP8266 runtime adapter no longer reads `next_tick_ms` or `next_tick_100ms_ms`; it asks `ev_demo_app_next_deadline_ms()`, which delegates to `ev_runtime_graph_next_deadline_ms()`.

## Commit 5 delivery migration

Actor-level compatibility delivery now consults `ev_runtime_graph_active_routes()` for the `(event_id, target_actor)` pair before sending. Optional-disabled route counters remain available for demo compatibility, but their source is the framework active route state rather than a parallel actor-enabled check.

## Commit 6 quiescence migration

Demo sleep decisions now call `ev_runtime_is_quiescent_at()` and use actor quiescence callbacks for OLED flush and DS18B20 conversion blockers. `pending_log_records` remains zero until a dedicated log flush-pending hook is added in the post-migration work.

## Commit 7 static-contract promotion

Demo migration blockers are now hard static-contract failures. The checker fails on reintroduced demo-owned mailboxes, actor runtimes, registry, domain/system pumps, legacy tick fields or adapter reads of old tick fields.
