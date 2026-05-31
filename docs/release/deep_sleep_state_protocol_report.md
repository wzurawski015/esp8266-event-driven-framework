# Deep sleep state protocol report

This patch promotes deep sleep to an explicit portable protocol. It does not change route semantics, mailbox layout, QoS, performance budgets, actor placement, zero-copy payload contracts, or runtime graph storage.

Added: `ev_power_state_machine`, `test_power_state_machine`, power actor transition counters, and documentation.

Failure behavior is explicit: guard/arming failures reject before platform prepare; log/prepare/deep-sleep failures enter `FAILED`; prepare/deep-sleep failures disarm/cancel as appropriate. The state machine is fixed-size and has no hot-path RAM impact.
