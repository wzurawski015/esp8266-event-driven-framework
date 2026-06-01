# QoS COALESCED/LATEST_ONLY promotion report

## Scope

This patch promotes `EV_ROUTE_QOS_COALESCED` and `EV_ROUTE_QOS_LATEST_ONLY`
from documented fallback classes into real bounded mailbox algorithms. It does
not change route fan-out, actor placement, mailbox capacities, active route
spans, graph storage, deep sleep, SDK/HIL evidence, performance budgets, or the
private-repo secrets policy.

## Runtime semantics

| QoS | Promoted behavior | Full-mailbox fallback |
|---|---|---|
| `EV_ROUTE_QOS_COALESCED` | If the target mailbox already contains an unconsumed message with the same event id, the incoming message is coalesced. Queue depth does not grow and the incoming payload is not retained. | If no matching event id is pending and the mailbox is full, the incoming message is dropped through the explicit drop-allowed policy. |
| `EV_ROUTE_QOS_LATEST_ONLY` | If the target mailbox already contains an unconsumed message with the same event id, the old slot is disposed and replaced by the newest message. The new message is retained before the old slot is modified. | If no matching event id is pending and the mailbox is full, the incoming message is dropped through the explicit drop-allowed policy. |

The algorithms operate only over the existing bounded mailbox storage. They do
not allocate heap memory and do not change any configured actor mailbox capacity.

## Payload lifetime safety

`LATEST_ONLY` replacement retains the new message before touching the old slot.
If retain fails, the old slot remains intact. On success, the old slot is
disposed exactly once and the new retained queue copy is stored in the same
slot. If a defensive store failure ever occurs after retain, the retained share
is released before returning the error.

`COALESCED` does not take ownership of the incoming message when a matching
pending event exists. This is intentional: the existing slot represents the
pending work item, and the incoming event is considered semantically handled by
coalescing.

## Delivery visibility

`ev_mailbox_push_qos()` reports one of:

- `EV_MAILBOX_DELIVERY_POSTED`,
- `EV_MAILBOX_DELIVERY_COALESCED`,
- `EV_MAILBOX_DELIVERY_REPLACED`,
- `EV_MAILBOX_DELIVERY_DROPPED`,
- `EV_MAILBOX_DELIVERY_REJECTED`.

`ev_delivery_report_t` now exposes:

- `coalesced`,
- `replaced`,
- `qos_dropped`,
- `mailbox_policy_rejected`.

Successful coalescing and latest-only replacement are counted as delivered
outcomes, not silent drops. The bounded full-mailbox fallback is visible through
`dropped` and `qos_dropped`.

## Metrics

The runtime adds counters for:

- `EV_METRIC_QOS_COALESCED`,
- `EV_METRIC_QOS_REPLACED`,
- `EV_METRIC_QOS_DROPPED`.

These counters make policy outcomes observable without turning valid
coalescing/replacement into faults.

## Tests and gates

The new `tests/host/test_qos_mailbox_algorithms.c` covers:

- repeated `COALESCED` event ids without queue growth,
- `COALESCED` behavior when the mailbox is full and a matching event exists,
- `LATEST_ONLY` inline replacement,
- `LATEST_ONLY` lease retain/release correctness,
- retain failure preserving the old slot,
- full mailbox matching replacement and non-matching drop fallback,
- actor registry QoS delivery effects.

The deterministic fuzz smoke now exercises `EV_ROUTE_QOS_COALESCED` and
`EV_ROUTE_QOS_LATEST_ONLY` mailbox paths and checks bounded mailbox capacity
invariants.

## Resource impact

Mailbox capacities and mailbox storage are unchanged. The only expected static
RAM increase is the metric registry growth from adding three QoS counters plus
the associated opaque graph storage padding update. No heap allocation was added.

## Validation expectation

The patch is expected to keep these gates green:

- `qos_contract_check.py`,
- `static_contracts.py`,
- `hotpath-zero-alloc-gate`,
- `host-test`,
- `host-strict-test`,
- `host-sanitize-test`,
- `host-tsan-test`,
- `quality-gate`,
- `perf-gate`.
