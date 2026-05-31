# Hard layering contract

This document is the architectural law for the framework. It is intentionally
stricter than the current implementation so that future refactors can move from
known, named exceptions toward enforceable boundaries. It does not add product
features and does not change runtime semantics.

Runtime data may cross a boundary only through an explicit public contract:
generated catalogs, actor messages, module descriptors, runtime graph APIs, port
interfaces, BSP profiles, or adapter-owned bootstrap wiring.

## Status model

Each layer section uses the same fields:

- **Responsibility**: stable reason for the layer to exist.
- **Allowed dependencies**: headers, generated artifacts, or neighboring layers
  the layer may know directly.
- **Forbidden dependencies**: knowledge that would collapse the boundary.
- **ESP8266 RTOS SDK visibility**: whether direct SDK headers/APIs are allowed.
- **BSP visibility**: whether board profile data may be included directly.
- **Concrete device actor visibility**: whether named device actor APIs may be
  referenced directly.
- **Hot path**: whether the layer participates in bounded dispatch, publish,
  mailbox, IRQ-adjacent, or per-poll execution.
- **Bootstrap-only**: whether use is restricted to graph construction, target
  startup, or tooling.
- **Current technical exceptions**: known deviations accepted in this commit.
- **Migration direction**: next intended hardening movement.

## config/codegen

- **Responsibility**: maintain single-source-of-truth definitions in `config/*.def`
  and generate deterministic C headers and documentation from those definitions.
- **Allowed dependencies**: repository-local generators under `tools/`, plain C
  preprocessor schema files, generated headers under `core/generated/include/ev/`,
  and host-only audit scripts.
- **Forbidden dependencies**: runtime state, platform adapters, BSP profiles,
  ESP8266 SDK APIs, mutable application policy, and hand-edited generated output.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden, except generated documentation may name BSP
  identifiers as inert text.
- **Concrete device actor visibility**: allowed only as catalog data declared in
  SSOT files; codegen must not depend on actor implementation files.
- **Hot path**: no.
- **Bootstrap-only**: yes; code generation is build-time/bootstrap-time only.
- **Current technical exceptions**: generated route and mailbox layout headers are
  committed so host and SDK builds can validate deterministic output.
- **Migration direction**: keep strengthening generators and audits until every
  runtime table has one canonical source and generated freshness is checked by CI.

## core kernel

- **Responsibility**: provide portable actor-kernel primitives: message contract,
  identifiers, mailbox operations, actor runtime, route table, delivery helpers,
  pumps, lease pool, result codes, and platform-independent framework services.
- **Allowed dependencies**: standard C headers, `core/include`, generated core
  headers, and abstract `ports/include` contracts when the kernel needs a stable
  platform boundary.
- **Forbidden dependencies**: ESP8266 SDK headers, BSP board profiles, adapter
  headers, application code, target build files, and hidden ownership of concrete
  platform resources.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden.
- **Concrete device actor visibility**: forbidden. Core may keep only actor
  kernel primitives such as IDs, catalogs, mailbox/runtime plumbing, and generic
  dispatch helpers.
- **Hot path**: yes. Core mailbox, publish, send, dispose, pump, and route lookup
  functions are hot path and must remain bounded, non-blocking, and static-memory
  friendly.
- **Bootstrap-only**: no.
- **Current technical exceptions**: none for concrete device/framework actor
  placement; static contracts now fail if those implementations or headers return
  to `core/`.
- **Migration direction**: keep `core/` as actor-kernel primitives only and move
  future concrete actor behavior directly into the actor layer.

## runtime

- **Responsibility**: own the canonical runtime graph, actor instances, scheduler,
  active route table, timer service, ingress service, delivery service,
  quiescence, power manager, metrics, fault bus, trace ring, command security,
  and network outbox composition.
- **Allowed dependencies**: core kernel APIs, generated catalogs, abstract ports,
  module descriptors, and application-provided configuration passed through
  public builder or graph contracts.
- **Forbidden dependencies**: direct BSP includes, ESP8266 SDK headers, target
  build assumptions, HIL fixtures, adapter internals, and application-owned
  scheduler/mailbox state.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden; board data must enter through explicit runtime
  profile structs or builder input.
- **Concrete device actor visibility**: only through descriptors/catalogs and actor
  function pointers. Runtime must not special-case a device actor by name.
- **Hot path**: yes. Runtime poll, scheduler, active route lookup, delivery,
  timers, ingress, outbox, trace, metrics, and graph-owned mailbox access all
  participate in bounded execution.
- **Bootstrap-only**: partly. Builder and graph initialization are bootstrap-only;
  scheduler, delivery, ingress, timer, trace, metrics, and outbox code are runtime
  hot path.
- **Current technical exceptions**: public code owns only bounded opaque runtime
  graph storage; the actual graph layout is internal to `runtime/src`. Delivery
  trace timestamps are read from the monotonic clock port with zero only as an
  explicit fallback.
- **Migration direction**: keep the graph boundary opaque and move remaining
  policy-heavy application code into small app-local policy, wiring, and
  presentation units.

## actor descriptors / module registry

- **Responsibility**: map actor identities to contexts, handlers, domains,
  mailbox policies, route participation, and module-level composition metadata.
- **Allowed dependencies**: `config/actors.def`, `config/modules.def`, generated
  actor catalogs, module descriptor headers, and runtime graph registration APIs.
- **Forbidden dependencies**: ESP8266 SDK APIs, BSP pin maps, adapter state,
  target-specific startup policy, and divergent duplicate metadata.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden.
- **Concrete device actor visibility**: descriptor rows may name actor entry
  points, but registry logic must treat them uniformly as descriptors.
- **Hot path**: mostly no; descriptor tables are read during bootstrap and lookup.
- **Bootstrap-only**: mostly yes, except generated catalog lookup data used during
  dispatch and delivery.
- **Current technical exceptions**: descriptor wrappers in `modules/` still
  include concrete actor public headers, but those headers now live under
  `actors/device/include` or `actors/framework/include`.
- **Migration direction**: keep actor/module consistency strict and consider
  moving descriptor wrappers into the actor layer only in a separate API-neutral
  cleanup.

## device actors

- **Responsibility**: implement reusable actor behavior for hardware-adjacent or
  service-adjacent entities. `actors/device` owns RTC, DS18B20, MCP23008, OLED,
  and panel actors. `actors/framework` owns network, command, power, supervisor,
  and watchdog actors.
- **Allowed dependencies**: core actor/message APIs, abstract port contracts,
  small driver facades, actor descriptors, and generated identifiers.
- **Forbidden dependencies**: direct ESP8266 SDK APIs, direct BSP pin maps, target
  build scripts, adapter-owned handles, unbounded blocking, and heap allocation.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden.
- **Concrete device actor visibility**: yes inside the device actor layer and its
  descriptor wrappers. Other layers should treat actors through catalogs and
  module descriptors.
- **Hot path**: yes. Actor handlers execute in cooperative runtime polling and
  must remain bounded.
- **Bootstrap-only**: no.
- **Current technical exceptions**: none for actor source/header placement;
  concrete actor files live in `actors/device` and `actors/framework`.
- **Migration direction**: keep actor behavior portable, bounded, and SDK-free;
  later split device-driver facades or module wrappers only as separate small
  changes.

## drivers

- **Responsibility**: expose portable driver-facing facades and thin compile-time
  grouping for device actor support without owning platform resources.
- **Allowed dependencies**: core actor contracts, device actor public headers,
  abstract ports, and C standard headers.
- **Forbidden dependencies**: ESP8266 SDK includes, BSP board profiles, adapter
  handles, runtime graph internals, application policy, and target build scripts.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden.
- **Concrete device actor visibility**: allowed for driver/device actor coupling,
  but not for runtime orchestration.
- **Hot path**: potentially yes when called by actors; driver facades must stay
  bounded and non-allocating.
- **Bootstrap-only**: no.
- **Current technical exceptions**: current driver headers mostly re-export actor
  headers from the actor layer.
- **Migration direction**: convert re-export facades into clearer actor-driver
  contracts or remove redundant wrappers in a separate cleanup.

## ports

- **Responsibility**: define stable abstract platform contracts for time, GPIO,
  IRQ, I2C, 1-Wire, UART, networking, reset, logging, watchdog, and system power.
- **Allowed dependencies**: standard C headers and small core result/message types
  needed to describe portable contracts.
- **Forbidden dependencies**: ESP8266 SDK headers, BSP pin maps, adapters,
  concrete actors, target build files, and runtime graph internals.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: forbidden.
- **Concrete device actor visibility**: forbidden.
- **Hot path**: yes when called by runtime, adapters, drivers, or actors; port
  contracts must describe bounded semantics.
- **Bootstrap-only**: no.
- **Current technical exceptions**: `port_net.h` includes `ev/msg.h` so network
  ingress can carry normalized messages; this is a public core contract.
- **Migration direction**: keep ports narrow and make them the only route from
  portable code to platform mechanisms.

## apps

- **Responsibility**: provide composition roots, example policy, board-neutral
  behavior, and application-specific actor contexts.
- **Allowed dependencies**: public core/runtime/module/port APIs, generated IDs,
  BSP-neutral configuration structs, and application-owned policy.
- **Forbidden dependencies**: ESP8266 SDK headers, target build scripts, adapter
  internals, direct runtime scheduler/timer/mailbox ownership, and hidden global
  platform state.
- **ESP8266 RTOS SDK visibility**: forbidden.
- **BSP visibility**: preferably forbidden; any board choice must be explicit
  composition input rather than a portable app default.
- **Concrete device actor visibility**: allowed only when composing an example or
  declaring actor contexts.
- **Hot path**: yes for app actor handlers and policy callbacks.
- **Bootstrap-only**: composition code is bootstrap-only; actor behavior is runtime
  code.
- **Current technical exceptions**: demo code still owns opaque
  `ev_runtime_graph_t` storage and concrete actor contexts as the designated
  composition root. Demo policy, board wiring, and presentation are now split
  from `ev_demo_app.c` so the root no longer acts as a monolithic framework.
- **Migration direction**: keep apps as composition roots and move the next
  hardening effort to host safety gates, zero-copy payload contracts, QoS, and
  formal deep-sleep protocol work.

## adapters

- **Responsibility**: bind abstract ports and runtime bootstrap to the concrete
  ESP8266 RTOS SDK, FreeRTOS, UART, Wi-Fi, MQTT, GPIO, I2C, 1-Wire, watchdog,
  logging, and reset mechanisms.
- **Allowed dependencies**: ESP8266 RTOS SDK headers, FreeRTOS APIs, BSP profiles,
  port contracts, runtime public APIs, and target-local bootstrap configuration.
- **Forbidden dependencies**: portable-layer backdoors, unallowlisted dynamic
  allocation, unbounded blocking in callbacks, application policy hidden inside
  platform glue, and direct mutation of runtime internals outside public init
  contracts.
- **ESP8266 RTOS SDK visibility**: allowed.
- **BSP visibility**: allowed.
- **Concrete device actor visibility**: only through app/runtime bootstrap and BSP
  composition; adapters should not implement actor business logic.
- **Hot path**: yes for IRQ-adjacent ingress, network ingress, logging, and port
  calls. Bootstrap-only SDK calls must stay allowlisted.
- **Bootstrap-only**: partly. SDK initialization, task creation, Wi-Fi setup, and
  MQTT setup are bootstrap-only unless explicitly modeled as bounded port calls.
- **Current technical exceptions**: adapter bootstrap primitives are permitted only
  through `tools/audit/adapter_exception_allowlist.def`.
- **Migration direction**: reduce adapter knowledge of graph internals and keep
  platform behavior behind narrow port implementations.

## bsp

- **Responsibility**: describe board profiles, logical pins, board capabilities,
  and compile-time board-level constants.
- **Allowed dependencies**: public BSP schema, port-facing logical identifiers,
  generated capability names when needed, and local private board profile data.
- **Forbidden dependencies**: runtime graph internals, scheduler state, adapter
  handles, actor implementation state, and SDK startup side effects.
- **ESP8266 RTOS SDK visibility**: normally forbidden in BSP headers; board data
  should stay declarative.
- **BSP visibility**: yes, this is the BSP layer.
- **Concrete device actor visibility**: forbidden; BSP should describe hardware,
  not actor policy.
- **Hot path**: no. BSP data is compile-time or bootstrap-time input.
- **Bootstrap-only**: yes.
- **Current technical exceptions**: the Wemos ESP-WROOM-02 18650 profile contains
  a private, deliberately tracked `board_secrets.local.h` so `git clean -fdx`
  does not remove the active lab Wi-Fi profile. This is a private-repository
  exception to normal secret hygiene and must be removed before publishing or
  sharing the repository.
- **Migration direction**: keep all normal targets secret-free by default;
  document private exceptions explicitly and avoid expanding them.

## tests

- **Responsibility**: verify contracts, generated metadata, bounded behavior,
  route/delivery semantics, runtime graph construction, adapter isolation, and
  migration blockers through host, property, and HIL-oriented tests.
- **Allowed dependencies**: public framework APIs, test fakes, generated catalogs,
  controlled white-box access when explicitly named by a migration test, and
  tooling outputs.
- **Forbidden dependencies**: production-only secrets, uncontrolled wall-clock
  behavior, hidden network assumptions, and test-only paths required by production
  code.
- **ESP8266 RTOS SDK visibility**: host/property tests should not require it; HIL
  tests may interact through target tools and serial logs.
- **BSP visibility**: allowed for BSP profile tests and HIL fixtures.
- **Concrete device actor visibility**: allowed in focused tests.
- **Hot path**: no production hot path, but tests should measure or assert hot
  path constraints.
- **Bootstrap-only**: no.
- **Current technical exceptions**: some tests intentionally validate migration
  blockers and may inspect public graph fields while the graph remains public.
- **Migration direction**: convert white-box tests to accessor-based tests as graph
  internals become opaque.

## tools

- **Responsibility**: provide generators, audits, build wrappers, SDK matrix
  checks, memory budget checks, documentation generation, and developer workflow
  automation.
- **Allowed dependencies**: repository metadata, config SSOT files, generated
  headers, documentation, build target metadata, and host Python/C tooling.
- **Forbidden dependencies**: runtime side effects, hardware assumptions unless the
  command is explicitly HIL/SDK scoped, and printing secrets.
- **ESP8266 RTOS SDK visibility**: allowed only in SDK/HIL workflow wrappers or
  scripts that explicitly validate SDK target metadata.
- **BSP visibility**: allowed for metadata validation and workflow generation.
- **Concrete device actor visibility**: allowed for catalog or consistency audits,
  not for production behavior.
- **Hot path**: no.
- **Bootstrap-only**: yes for generators and build tools; audits are quality gates.
- **Current technical exceptions**: `tools/fw` also orchestrates developer-local
  workflows, so commands must remain careful about not leaking secrets.
- **Migration direction**: turn this written contract into incremental static
  checks without breaking known temporary exceptions.

## docs

- **Responsibility**: record architecture, contracts, ADRs, release reports,
  migration plans, known limitations, and validation evidence.
- **Allowed dependencies**: repository facts, generated catalogs, validation logs,
  and explicit references to current exceptions.
- **Forbidden dependencies**: undisclosed secrets, unverifiable claims, stale
  architecture promises that contradict enforced gates, and feature-roadmap drift.
- **ESP8266 RTOS SDK visibility**: docs may describe SDK boundaries and target
  workflows, but should not require SDK code execution.
- **BSP visibility**: docs may describe BSP profiles and exceptions.
- **Concrete device actor visibility**: docs may name concrete actors as examples
  or migration targets.
- **Hot path**: no.
- **Bootstrap-only**: no.
- **Current technical exceptions**: historical release reports may describe earlier
  migration states; this file is authoritative for new refactors.
- **Migration direction**: keep architecture docs discoverable from the overview
  and static-contract specs, then align release reports after each hardening step.

## Known temporary exceptions

The following exceptions are intentionally named instead of hidden:

1. `ev_runtime_graph_t` is public opaque static storage for embedded stack/static
   ownership, but direct non-runtime field access is an enforced violation.
2. The Wemos ESP-WROOM-02 18650 BSP has a private, deliberately tracked
   `board_secrets.local.h`; this is not a general policy and must not be copied
   to public repositories.

## Enforcement roadmap

1. Keep `tools/audit/static_contracts.py` checking that this document exists and
   preserves the main layer sections.
2. Make the `ev_runtime_graph_t` storage contract explicit or fully opaque.
3. Enforce route delivery policies end to end.
4. Add host microbenchmarks for publish and runtime polling.
5. Keep concrete device/framework actors out of `core/` with static contracts.
6. Reduce demo apps to pure composition roots and example policy.
