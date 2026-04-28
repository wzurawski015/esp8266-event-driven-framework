# Power policy

The framework-level power manager evaluates sleep eligibility through:

- configured policy,
- quiescence report,
- timer next deadline,
- board/runtime capability state.

`ev_power_policy_t` includes fields for timed sleep, one-way sleep, GPIO16 wake support, min/max duration, required wake sources, required quiescence mask, log flush policy, device park policy, and wake reason policy.
