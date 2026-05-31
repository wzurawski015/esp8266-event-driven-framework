# Deep sleep state transition protocol

Deep sleep is modeled as an explicit portable state machine rather than an ad-hoc actor call sequence.

```text
ACTIVE -> SLEEP_REQUESTED -> DRAINING_RUNTIME -> LOG_FLUSHING -> PORTS_PREPARE_SLEEP -> RTC_STATE_SAVED -> ENTERING_DEEP_SLEEP
```

Diagnostic terminals are `REJECTED` and `FAILED`; `WAKE_BOOT` may transition back to `ACTIVE` during host modeling.
The state-machine core does not allocate, include ESP8266 SDK headers, block, or log. Platform operations remain behind ports.
The current RTC state save step is a no-op marker, intentionally visible in tests so a future RTC callback can be added without changing the sequence.
