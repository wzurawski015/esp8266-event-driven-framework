# Fault bus

The fault bus is generated from `config/faults.def` and exposes `EV_FAULT_REPORTED` and `ACT_FAULT` as first-class diagnostic elements.

`ev_fault_payload_t` records:

- fault code,
- severity,
- source actor,
- source module,
- triggering event,
- timestamp,
- two numeric arguments,
- coalescing counter,
- flags.

The fault registry is bounded and coalesces repeated faults with the same fault/source/event tuple.
