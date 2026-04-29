# esp8266_event-driven framework

`esp8266_event_driven_framework` is a C11, static-memory, event-driven framework for ESP8266 RTOS SDK style systems. It was generated from the existing `esp8266-event-driven_20260428_142913` codebase and refactored so that framework mechanisms no longer live only in the former demo application layer.

The project preserves the existing actor runtime, mailbox, route table, message ownership model, lease pool, ports, BSP profiles, ESP8266 adapter skeletons, fakes, and host tests, while adding a reusable runtime layer.

## What is new

The new framework layer adds:

- `runtime/` with runtime graph, runtime builder, bounded poll coordinator, timer/deadline service, ingress service, quiescence service, delivery service, power manager, fault bus, metrics registry, trace ring, command security, and network outbox backpressure.
- `modules/` as reusable module-facing actor wrappers.
- `drivers/` as device actor wrapper layer.
- `apps/` split into `apps/atnel_air`, `apps/demo`, and `apps/minimal`.
- Extended `EV_ROUTE_EX(event, actor, qos, priority, flags)` route declarations while preserving `EV_ROUTE(event, actor)`.
- Static module descriptors from `config/modules.def`.
- Fault and metrics catalogs wired into runtime services.
- Host/property tests for the new services.
- Static contract and memory-budget gates.

## Host validation

Run:

```sh
make clean
make routegen
make routegen-check
make static-contracts
make memory-budget
make host-test
make property-test
make quality-gate
```

`make quality-gate` executes the host release gate:

```sh
make clean
make routegen-check
make static-contracts
make memory-budget
make host-test
make property-test
```

## ESP8266 SDK and HIL

ESP8266 RTOS SDK target skeletons remain under:

```text
adapters/esp8266_rtos_sdk/
```

SDK firmware builds require an installed ESP8266 RTOS SDK and Xtensa toolchain. Hardware-in-the-loop tests require physical boards and serial profiles.

## Layering

```text
core/      foundational primitives and generated catalogs
runtime/   reusable framework runtime services
modules/   reusable framework actor-module layer
drivers/   device actor wrapper layer
apps/      concrete applications and examples
ports/     portable port contracts
adapters/  ESP8266 RTOS SDK integration
bsp/       board profiles and pins
tools/     routegen, audit, HIL, and helper tools
docs/      architecture, specs, and release reports
```

The compatibility demo entry remains only under `apps/demo/ev_demo_app.c`; it is not the framework runtime root.

## Runtime graph migration readiness

This revision prepares `ev_runtime_graph_t` as the canonical framework kernel
without yet deleting the compatibility composition root in `apps/demo`. Run:

```sh
make release-gate
```

to execute the host quality gate plus generated documentation checks.

## Runtime graph migration status

The demo application now uses `ev_runtime_graph_t` as the owner of runtime mailboxes, actor runtimes, registry and scheduler. Remaining production hardening work is tracked in `docs/release/demo_runtime_graph_migration_final_report.md`.
