# Wemos smoke and deep-sleep/wake evidence report

| Area | Status |
|---|---:|
| Wemos smoke | ENVIRONMENT_BLOCKED |
| Wemos deep-sleep/wake | ENVIRONMENT_BLOCKED |

This patch adds firmware markers, strict parsers, Makefile targets, and release reports. It does not declare PASS without real Wemos serial evidence.

Required deep-sleep sequence:

```text
EV_WEMOS_SMOKE_BOOT
EV_WEMOS_SMOKE_RUNTIME_READY
EV_WEMOS_SMOKE_TICK seq=<n>
EV_WEMOS_SMOKE_SNAPSHOT seq=<n>
EV_POWER_SMOKE_STATE ACTIVE
EV_POWER_SMOKE_STATE SLEEP_REQUESTED
EV_POWER_SMOKE_STATE DRAINING_RUNTIME
EV_POWER_SMOKE_STATE LOG_FLUSHING
EV_POWER_SMOKE_STATE PORTS_PREPARE_SLEEP
EV_POWER_SMOKE_STATE RTC_STATE_SAVED
EV_POWER_SMOKE_STATE ENTERING_DEEP_SLEEP
EV_POWER_SMOKE_DEEP_SLEEP_ENTER
EV_POWER_SMOKE_WAKE_BOOT
```

Remaining external evidence for perfection: real current/power measurement during deep sleep and wake, in addition to serial markers.
