# QoS end-to-end enforcement report

This patch series centralizes route QoS behavior in `ev_qos_contract`, validates
route/module compatibility before active delivery, and exposes `rejected_routes`
plus `qos_conflict_routes` in delivery reports.

The current snapshot also promotes `EV_ROUTE_QOS_COALESCED` and
`EV_ROUTE_QOS_LATEST_ONLY` to real bounded mailbox algorithms:

- `COALESCED` coalesces repeated pending event ids without growing queue depth.
- `LATEST_ONLY` replaces a repeated pending event id with the newest message,
  including safe retain/release handling for leased payloads.

The algorithms are bounded, use no heap, preserve mailbox capacities, and report
outcomes through delivery report fields and metrics. The patch does not change
the deep sleep state machine, route fanout semantics, actor placement, graph
storage, demo composition-root structure, hotpath zero-allocation rules, SDK/HIL
evidence workflow, private-repo secrets policy, or perf budgets.
