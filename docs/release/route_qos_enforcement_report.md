# Route QoS enforcement report

Date: 2026-05-25
Scope: minimal runtime QoS policy hardening and tests only.

## Input state

The analysed archive was:

```text
esp8266-event-driven-framework_20260525_141148.tar.gz
```

Its SHA-256 was:

```text
d88ae61bea5c3ac5bb0236aaee33806b018de3eb1fbce65c2cdd711d35924255
```

Baseline audits passed for route generation, mailbox layout freshness, routegen
consistency, static contracts, actor/module descriptor consistency, memory
budget, and SDK target matrix metadata.

## Observed pre-patch state

- `EV_ROUTE_QOS_*` values already existed in `core/include/ev/route_table.h`.
- `EV_ROUTE_EX(...)` was already supported by route generation.
- `runtime/src/ev_delivery_service.c` already had drop-allowed handling for
  selected QoS classes, but strict delivery behavior was still encoded as a
  repeated hand-written `CRITICAL || WAKEUP_CRITICAL || COMMAND` condition.
- `config/routes.def` still declared most routes through `EV_ROUTE(...)`, which
  makes them critical by default. Before this patch, only `EV_FAULT_REPORTED`
  was explicitly declared through `EV_ROUTE_EX(...)`.
- `config/modules.def` still uses `route_policy_flags` as a historical field
  name. It currently behaves like one accepted QoS class with compatibility
  allowances, not as a true flag set.
- The repository already contained the prior foundation work: layering contract,
  generated mailbox layout, active route spans, and runtime graph accessor
  boundary.

## Changes introduced

- Added a public delivery-service QoS failure policy API.
- Made invalid or unknown QoS values fail strict by default.
- Replaced duplicated strict QoS disjunctions in delivery with calls to the
  central policy API.
- Marked the following routes explicitly:
  - `EV_TIME_UPDATED -> ACT_NETWORK` as telemetry,
  - `EV_TEMP_UPDATED -> ACT_NETWORK` as telemetry,
  - `EV_SYS_GOTO_SLEEP_CMD -> ACT_POWER` as wakeup-critical,
  - `EV_NET_MQTT_MSG_RX -> ACT_COMMAND` as command,
  - `EV_NET_MQTT_MSG_RX_LEASE -> ACT_COMMAND` as command.
- Added `tests/host/test_route_qos_delivery_policy.c` to verify the public
  policy table, explicit route declarations, drop-allowed mailbox failure, and
  strict mailbox failure.
- Updated route QoS and static-contract documentation.
- Added a narrow static-contract check for the release report and for absence of
  the old hand-coded strict QoS disjunction in the delivery service.

## Deliberately out of scope

- No SDK or HIL behavior was changed.
- No Wi-Fi workflow or secret file was changed.
- No application feature was added.
- No actor was moved out of `core/`.
- No new coalesced/latest-only delivery algorithm was introduced.
- No trace timestamp work was done; `trace.timestamp_us` remains follow-up work.

## Known limitations retained intentionally

- `route_policy_flags` remains a historical name and is still closer to a single
  accepted QoS class than to a real bitset.
- `COALESCED` and `LATEST_ONLY` currently gain only drop-allowed failure policy;
  their concrete runtime behavior is still defined by mailbox kind.
- Delivery trace timestamps still require the separate monotonic-clock-port
  follow-up.
- `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h` is a private,
  deliberately tracked lab exception to normal secret hygiene. This patch does
  not print, edit, add, or remove secrets.

## Recommended next step

The next safest architectural step remains:

```text
perf: add host microbenchmarks for publish and runtime poll
```

A performance gate should be added only after this QoS semantics patch lands,
because publish semantics and expected drop/strict behavior are then explicit and
covered by host tests.
