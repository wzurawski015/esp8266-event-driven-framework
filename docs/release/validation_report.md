# Validation report

Baseline archive: `esp8266-event-driven_20260430_080553.tar.gz`.

This report records the host-side validation state after the demo runtime-graph migration. The follow-up hardening series removes remaining compatibility seams around graph internals, poll orchestration, actor emission, diagnostics and quiescence log pending state.

## Executed gates

| Command | Result | Notes |
|---|---:|---|
| `make routegen-check PYTHON=python3` | PASS | 53 generated routes verified. |
| `make static-contracts PYTHON=python3` | PASS | Demo runtime ownership blockers are hard failures after this migration. |
| `make memory-budget PYTHON=python3` | PASS | Memory budget checker completed. |
| `make host-test PYTHON=python3` | PASS | All host tests in `HOST_TESTS` passed, including the new golden tests. |
| `make property-test PYTHON=python3` | PASS | Deterministic property tests passed. |
| `make quality-gate PYTHON=python3` | PASS | Clean + routegen/static/memory/host/property completed. |
| `make docgen PYTHON=python3` | PASS | Generated docs/catalog files. |
| `make docs PYTHON=python3` | NOT PASSED IN THIS ENVIRONMENT | `doxygen` executable is not installed in this container. |
| `make release-gate PYTHON=python3` | NOT PASSED IN THIS ENVIRONMENT | Depends on `make docs`; blocked by missing `doxygen`. |

## Targeted migration tests

The following new or migration-critical tests were built and executed as part of
host validation:

- `test_demo_app_boot_sequence_golden`
- `test_demo_app_disabled_route_golden`
- `test_demo_app_tick_order_golden`
- `test_demo_app_sleep_guard_golden`
- `test_demo_app_fairness_golden`
- `test_demo_runtime_instances`
- `test_demo_migration_blockers`
- `test_runtime_graph_canonical_scheduler`
- `test_runtime_timer_migration`
- `test_runtime_quiescence_time_aware`
- `test_runtime_disabled_routes`
- `test_runtime_graph_publish_send`

## SDK and HIL status

ESP8266 SDK build validation and HIL validation were not executed in this
environment. They must not be claimed until run on a configured SDK toolchain and
physical/self-hosted hardware runner.

## Remaining post-hardening work

1. SDK build matrix for all targets.
2. HIL for ATNEL I2C / OneWire / WiFi.
3. Wemos minimal runtime smoke on a real board.
4. Final memory budget from ESP8266 linker map.
5. Release report with real SDK/HIL results.
6. ADR wording review after hardware validation.

## Final status for executed host gates

For the host-side gates actually executed above, there are zero known critical defects after the executed gates.


## No-legacy hardening status

This report is updated by the hardening patch series. SDK and HIL validation are
not claimed unless run in a configured environment.
