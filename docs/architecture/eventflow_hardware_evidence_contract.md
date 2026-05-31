# Event-flow hardware evidence contract

The Event-Driven Reactor hardware gate aggregates real evidence for the asynchronous cycle:

```text
boot -> runtime ready -> actor tick/snapshot events -> I2C hardware fault -> containment/recovery -> sleep request -> quiescence accepted -> power state transitions -> deep sleep entry -> wake boot -> runtime ready again
```

The gate uses `config/eventflow_hardware_evidence.def` as its source of truth. Every PASS must be backed by parsed JSON and committed serial/build evidence; `ENVIRONMENT_BLOCKED` is preserved when hardware or SDK toolchains are not available.
