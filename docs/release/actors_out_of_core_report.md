# Actors out of core report

## Decision

Concrete device and framework actors were moved out of `core/` without changing
runtime semantics, route definitions, QoS policy, mailbox layout, active route
spans, trace timestamp behavior, graph storage, deep sleep behavior, or
application features.

`core/` remains the portable actor-kernel layer: actor IDs/catalogs, generic
actor runtime, message/mailbox/route primitives, dispatch helpers, pumps, result
codes, and other framework primitives.

## Moved device actors

| From | To |
|---|---|
| `core/src/ev_rtc_actor.c` | `actors/device/ev_rtc_actor.c` |
| `core/src/ev_ds18b20_actor.c` | `actors/device/ev_ds18b20_actor.c` |
| `core/src/ev_mcp23008_actor.c` | `actors/device/ev_mcp23008_actor.c` |
| `core/src/ev_oled_actor.c` | `actors/device/ev_oled_actor.c` |
| `core/src/ev_panel_actor.c` | `actors/device/ev_panel_actor.c` |
| `core/include/ev/rtc_actor.h` | `actors/device/include/ev/rtc_actor.h` |
| `core/include/ev/ds18b20_actor.h` | `actors/device/include/ev/ds18b20_actor.h` |
| `core/include/ev/mcp23008_actor.h` | `actors/device/include/ev/mcp23008_actor.h` |
| `core/include/ev/oled_actor.h` | `actors/device/include/ev/oled_actor.h` |
| `core/include/ev/panel_actor.h` | `actors/device/include/ev/panel_actor.h` |

## Moved framework actors

| From | To |
|---|---|
| `core/src/ev_network_actor.c` | `actors/framework/ev_network_actor.c` |
| `core/src/ev_command_actor.c` | `actors/framework/ev_command_actor.c` |
| `core/src/ev_power_actor.c` | `actors/framework/ev_power_actor.c` |
| `core/src/ev_watchdog_actor.c` | `actors/framework/ev_watchdog_actor.c` |
| `core/src/ev_supervisor_actor.c` | `actors/framework/ev_supervisor_actor.c` |
| `core/include/ev/network_actor.h` | `actors/framework/include/ev/network_actor.h` |
| `core/include/ev/command_actor.h` | `actors/framework/include/ev/command_actor.h` |
| `core/include/ev/power_actor.h` | `actors/framework/include/ev/power_actor.h` |
| `core/include/ev/watchdog_actor.h` | `actors/framework/include/ev/watchdog_actor.h` |
| `core/include/ev/supervisor_actor.h` | `actors/framework/include/ev/supervisor_actor.h` |

## Kernel actor primitives left in core

| File | Reason |
|---|---|
| `core/src/ev_actor_catalog.c` | Generated actor metadata lookup belongs to the core actor kernel. |
| `core/src/ev_actor_runtime.c` | Generic runtime/mailbox pumping primitive, not a concrete actor. |
| `core/include/ev/actor_catalog.h` | Public actor metadata contract. |
| `core/include/ev/actor_id.h` | Generated actor identifier contract. |
| `core/include/ev/actor_runtime.h` | Generic actor runtime and registry contract. |

## Build and include path impact

The host Makefile now separates `CORE_SRCS`, `ACTOR_DEVICE_SRCS`, and
`ACTOR_FRAMEWORK_SRCS`. Host tests, property tests, quality gates, and benchmark
binaries link the actor sources through the common source list. The ESP8266 RTOS
SDK platform component also lists `actors/device` and `actors/framework` as
source directories and adds their public include roots.

The public include style remains `#include "ev/<actor>.h"`. No relative include
into `actors/` is required.

## RAM and hot path impact

This is a file-layout and build-wiring change. It does not change data
structures, static storage, route table entries, mailbox capacities, actor
handlers, or scheduler/delivery semantics. Expected RAM and hot-path performance
impact is therefore zero aside from normal compiler path determinism.

## Enforced contracts

`tools/audit/static_contracts.py` now fails when:

- concrete device/framework actor sources return to `core/src`,
- concrete actor headers return to `core/include/ev`,
- `core/` includes concrete actor headers,
- actor files include ESP8266 SDK headers directly,
- actor files introduce heap allocation or blocking primitives,
- code uses relative includes into `actors/`.

`tests/host/test_actor_layering_contract.c` verifies source/header placement,
module handler discovery, and a small runtime-builder binding path for a moved
actor.

## Validation summary

Run the standard post-patch gates from the companion analysis/apply notes:
private secrets policy, expected public release failure, descriptor consistency,
routegen checks, static contracts, release evidence, memory budget, host tests,
property tests, quality gate, SDK matrix check, perf gate, `git diff --check`,
and repository grep checks for actor placement and SDK leaks.
