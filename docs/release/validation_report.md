# Validation report

Date: 2026-04-28

Project: `esp8266_event_driven_framework`

## Toolchain observed

| Tool | Version |
|---|---|
| C compiler | `cc (Debian 14.2.0-19) 14.2.0` |
| Python | `Python 3.13.5` |
| Make | `GNU Make 4.4.1` |

## Commands executed

| Command | Status | Notes |
|---|---:|---|
| `make clean` | pass | Removed host build artifacts and restored `docs/generated/.gitkeep`. |
| `make routegen` | pass | Generated 53 route entries into `core/generated/include/ev/route_table_generated.h`. |
| `make routegen-check` | pass | Confirmed generated route count matches `config/routes.def`. |
| `make static-contracts` | pass | Checked SDK leakage, heap APIs, blocking APIs, routegen freshness, BSP profiles, pins schema, and source markers. |
| `make memory-budget` | pass | Wrote `docs/release/memory_budget_report.md`. |
| `make host-test` | pass | Original host tests plus new runtime-builder/timer/quiescence/fault/metrics/trace/delivery/command/network tests passed. |
| `make property-test` | pass | Deterministic xorshift property test for mailbox, timer, and trace behavior passed. |
| `make quality-gate` | pass | Serial release gate passed: clean, routegen-check, static-contracts, memory-budget, host-test, property-test. |
| `make clean` | pass | Packaging cleanup after validation; source and generated route table remained intact. |

Detailed command logs are stored in `docs/release/validation_*.log`.

## Warning status

The host compiler was run with the preserved project warning profile:

```text
-std=c11 -Wall -Wextra -O0 -g0 -pedantic
```

No source-level warning suppressions were added for the new framework layer. Global `-Werror` was not enabled for the full preserved legacy codebase; see `docs/release/warning_exceptions.md`.

## Static contract result

Status: pass.

The checker verified:

- no forbidden heap calls in the configured portable/framework layers,
- no `portMAX_DELAY`,
- no runtime/actor `vTaskDelay`,
- no ESP8266 SDK include leakage into portable layers,
- generated route count freshness,
- `faults.def` and `metrics.def` presence,
- BSP board profile presence,
- supported pins schema,
- no production source `TODO`/`FIXME` markers.

## Memory budget result

Status: pass.

Summary from `docs/release/memory_budget_report.md`:

- `ev_runtime_graph_t`: 26840 bytes
- `ev_msg_t`: 72 bytes
- `ev_mailbox_t`: 88 bytes
- `ev_actor_runtime_t`: 96 bytes
- `ev_timer_service_t`: 396 bytes
- `ev_trace_ring_t`: 1048 bytes
- `ev_fault_registry_t`: 664 bytes
- `ev_metric_registry_t`: 152 bytes
- `ev_network_outbox_t`: 640 bytes
- `mailbox_storage`: 19584 bytes

This is a host static-size probe. ESP8266 linker-map memory validation requires the SDK/toolchain.

## Generated artifact freshness

Status: pass.

`tools/routegen/routegen.py` generated the route table from `config/routes.def`; `tools/audit/routegen_check.py` confirmed the generated count.

## SDK and HIL validation

Status: not executed.

Reason:

- no ESP8266 RTOS SDK installation was available,
- no Xtensa ESP8266 compiler toolchain was available,
- no physical HIL board or serial profile was attached.

This is also recorded in `docs/release/sdk_validation_not_run.md`.

## Environment note

Python invocations in this notebook environment emitted a non-project startup message from `presentation_artifact_tool` spreadsheet warmup. Those messages appeared on stderr but did not change the return codes of routegen or audit commands. Commands were accepted only by process exit status.

## Archive verification status

Status: pass after packaging.

The final packaging step verifies:

```sh
tar -tzf esp8266_event_driven_framework.tar.gz | head
tar -tzf esp8266_event_driven_framework.tar.gz | grep '^esp8266_event_driven_framework/README.md$'
tar -tzf esp8266_event_driven_framework.tar.gz | grep '^esp8266_event_driven_framework/Makefile$'
tar -tzf esp8266_event_driven_framework.tar.gz | grep '^esp8266_event_driven_framework/docs/release/validation_report.md$'
```

## Result

Quality gate: pass.

Conclusion: zero known critical defects after the executed gates.
