# Power policy

The framework-level power manager evaluates sleep eligibility through:

- configured policy,
- quiescence report,
- timer next deadline,
- board/runtime capability state.

`ev_power_policy_t` includes fields for timed sleep, one-way sleep, GPIO16 wake support, min/max duration, required wake sources, required quiescence mask, log flush policy, device park policy, and wake reason policy.

## Demo runtime migration note

The demo power path uses the runtime quiescence policy as the sleep admission gate. The demo compatibility power guard may still format `ev_power_quiescence_report_t`, but the source of truth is the framework quiescence report and runtime timer deadline state.
