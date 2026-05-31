# Eventflow hardware release contract

The hardware event-flow gate aggregates the asynchronous path:

```text
SDK build -> flashable target -> boot -> runtime ready -> actor tick/snapshot -> I2C hardware fault -> containment/recovery -> sleep request -> quiescence/state transitions -> deep sleep entry -> wake boot
```

Every source is listed in `config/eventflow_hardware_evidence.def`. PASS requires parsed JSON,
source SHA-256 and source-specific marker validation. Missing hardware remains
`ENVIRONMENT_BLOCKED`; it is not softened into PASS.
