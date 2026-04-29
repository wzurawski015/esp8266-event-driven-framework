# Validation report

Baseline archive: `esp8266-event-driven_20260429_104401.tar.gz`.

This report records validation executed for the runtime-graph preparation patch
series. The full demo migration is intentionally deferred.

## Executed gates

| Command | Result | Notes |
|---|---:|---|
| `make routegen-check PYTHON=python3` | PASS | 53 generated routes verified. |
| `make static-contracts PYTHON=python3` | PASS | Reports demo migration blockers as `MIGRATION_BLOCKER_REPORTED`, not hard failures. |
| `make memory-budget PYTHON=python3` | PASS | Updated `docs/release/memory_budget_report.md`. |
| `make host-test PYTHON=python3` | PASS | All host tests in `HOST_TESTS` passed. |
| `make property-test PYTHON=python3` | PASS | Deterministic property tests passed. |
| `make docgen PYTHON=python3` | PASS | Generated docs/catalog files. |
| `make docs PYTHON=python3` | NOT PASSED IN THIS ENVIRONMENT | `doxygen` executable is not installed in this container. |

## Targeted runtime-preparation tests

The following new or migration-critical tests were built and executed as part of
host validation:

- `test_runtime_actor_instance_descriptors`
- `test_runtime_builder_route_validation`
- `test_runtime_disabled_routes`
- `test_runtime_graph_publish_send`
- `test_runtime_graph_canonical_scheduler`
- `test_runtime_quiescence_time_aware`
- `test_runtime_sequence_ingress`
- `test_runtime_sequence_network_outbox`
- `test_wemos_runtime_profile`
- `test_demo_migration_blockers`
- `test_runtime_builder_framework`
- `test_timer_quiescence_framework`
- `test_delivery_command_network_framework`
- `test_bsp_runtime_profile`
- `test_app_fairness`
- `test_app_starvation`
- `test_demo_app_sleep_quiescence`

## SDK and HIL status

ESP8266 SDK build validation and HIL validation were not executed in this
environment. They must not be claimed until run on a configured SDK toolchain and
physical/self-hosted hardware runner.

## Remaining intentional blocker

`apps/demo` still owns the legacy/manual composition root. This is intentional
for this patch series. The next iteration should perform:

```text
refactor(app): migrate demo app to runtime_graph without losing behavior
```

## Final status for executed gates

For the gates actually executed above, there are zero known critical defects after the executed gates.
