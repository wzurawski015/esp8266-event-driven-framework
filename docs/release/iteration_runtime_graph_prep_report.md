# Runtime graph preparation iteration report

Input baseline: `esp8266-event-driven_20260429_104401.tar.gz`.

## Verified pre-change state

The following migration blockers were intentionally verified before the changes:

- `apps/demo/include/ev/demo_app.h` still owns `ev_mailbox_t` fields, mailbox
  storage arrays, `ev_actor_runtime_t`, `ev_actor_registry_t`, `ev_domain_pump_t`,
  `ev_system_pump_t`, `next_tick_ms` and `next_tick_100ms_ms`.
- `apps/demo/ev_demo_app.c` still initializes mailbox storage, actor runtimes,
  registry bindings, domain/system pumps, disabled routes, ingress, tick timers
  and the compatibility poll loop.
- `runtime_builder_bind_routes()` was a placeholder before this iteration.
- `runtime/src/ev_runtime_poll.c` used direct actor iteration before this
  iteration.
- framework quiescence did not accept `now_ms` and did not compute due timers.
- ingress and network outbox were `head + count` rings.
- Wemos ESP-WROOM-02 18650 ran boot diagnostics only.

## Implemented scope

This patch series implements the maximum safe preparation scope without deleting
the demo compatibility runtime:

1. runtime ports, board profile and actor instance descriptors;
2. active route table and route validation;
3. graph publish/send API;
4. runtime scheduler wrapper around domain/system pumps;
5. time-aware quiescence policy API;
6. sequence-mask ingress ring;
7. sequence-mask network outbox;
8. Wemos minimal runtime profile;
9. migration-blocker and property tests;
10. release-gate, ADR and CI alignment.

## Deferred blocker

Full migration of `apps/demo` to `runtime_graph` remains intentionally deferred.
