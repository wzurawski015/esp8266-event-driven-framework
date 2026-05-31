# QoS end-to-end enforcement report

This patch centralizes route QoS behavior in `ev_qos_contract`, validates route/module compatibility before active delivery, and exposes `rejected_routes` plus `qos_conflict_routes` in delivery reports.
It does not change the deep sleep state machine, mailbox capacities, route fanout semantics, actor placement, graph storage, demo composition-root structure, hotpath zero-allocation rules, or perf budgets.

`EV_ROUTE_QOS_COALESCED` and `EV_ROUTE_QOS_LATEST_ONLY` remain drop allowed and algorithm-not-yet-promoted until a bounded mailbox replacement/coalescing algorithm is promoted with tests.
