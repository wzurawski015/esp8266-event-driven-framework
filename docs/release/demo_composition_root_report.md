# Demo composition-root refactor report

## Scope

This patch implements the `apps: reduce demo app to composition root and example policy` step.  It is an app-layer refactor only: it does not change runtime graph storage, actor placement, QoS, deep sleep state transitions, mailbox layout, active route spans, trace timestamp policy, or SDK/HIL target semantics.

## Before

`apps/demo/ev_demo_app.c` carried composition-root setup together with board profile validation, runtime graph wiring, app/diag actor policy, OLED presentation helpers, deep-sleep quiescence callbacks, watchdog liveness callbacks, ingress collection, and public app entry points.  The file was about 1809 lines in the input archive.

## After

`apps/demo/ev_demo_app.c` is now about 805 lines and keeps the app entry points, bounded poll loop, publish helpers, and small composition-root orchestration.  The extracted units are:

- `apps/demo/ev_demo_policy.c` and `apps/demo/include/ev/demo_policy.h` for app/diag actor behavior, deep-sleep admission callbacks, and watchdog liveness policy.
- `apps/demo/ev_demo_board_wiring.c` and `apps/demo/include/ev/demo_board_wiring.h` for board profile validation, runtime capability mapping, runtime builder wiring, publish-port setup, and standard timer scheduling.
- `apps/demo/ev_demo_presentation.c` and `apps/demo/include/ev/demo_presentation.h` for OLED text formatting, screensaver movement, and OLED scene commits.
- `apps/demo/include/ev/demo_internal.h` for app-local helper prototypes shared only by the demo split units.

Concrete actor initialization and runtime-builder wiring are outside `ev_demo_app.c`; they are concentrated in `ev_demo_board_wiring.c` behind the public `ev_demo_app_configure_runtime()` helper.

## Boundary decisions

The demo remains a composition root: it owns opaque runtime graph storage, actor contexts, board profile input, ports, and the lease pool.  It does not include `ev_runtime_graph_internal.h`, does not access graph internals, and does not include ESP8266 SDK headers.  Adapter-owned SDK wiring remains in `adapters/`.

## RAM and hot path impact

The refactor moves code between translation units and adds no heap allocation.  Runtime state structs, mailbox layout, actor contexts, route tables, and message sizes are unchanged.  Hot-path behavior is unchanged: publish, send, active route spans, scheduler polling, timer delivery, ingress collection, and trace timestamp behavior keep the same public APIs and semantics.

## Static contracts

`tools/audit/static_contracts.py` now checks that:

- the split demo policy/wiring/presentation files exist,
- `apps/demo/ev_demo_app.c` stays below the composition-root line limit,
- demo files do not include `ev_runtime_graph_internal.h`,
- demo files do not include ESP8266 SDK headers directly,
- demo code does not reintroduce legacy registry/domain/system pump initialization or direct scheduler polling.

## Host evidence

A new host test, `tests/host/test_demo_composition_root_contract.c`, initializes the demo through public app/config APIs, verifies deterministic default-policy init, publishes boot events, polls to quiescence, and confirms the presentation boundary rejects invalid input without graph-internal access.

## Validation checklist

Expected post-patch gates:

- private repo secrets policy: PASS,
- public release secrets policy: EXPECTED_FAIL when private secrets are present,
- descriptor consistency: PASS,
- routegen and mailbox layout: PASS,
- static contracts: PASS,
- memory budget: PASS,
- host tests: PASS,
- property tests: PASS,
- quality gate: PASS,
- SDK matrix metadata: PASS or environment-blocked if SDK metadata is unavailable,
- perf gate: PASS.
