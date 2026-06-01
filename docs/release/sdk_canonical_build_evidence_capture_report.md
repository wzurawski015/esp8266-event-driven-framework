# SDK canonical build evidence capture report

Status: canonical SDK capture hardening added.

The SDK capture path now requires target-specific canonical build markers:

```text
EV_SDK_BUILD_TARGET=<target>
EV_SDK_BUILD_PROJECT=<path>
EV_SDK_BUILD_BEGIN
EV_SDK_BUILD_STATUS=PASS
EV_SDK_BUILD_RC=0
EV_SDK_BUILD_END
```

For buildable, HIL and physical-smoke targets, `APP_BIN` must be non-zero and at least one real memory section marker must be non-zero. Memory-report PASS may support memory report diagnostics but is not SDK build proof.

Private-repo secrets remain allowed by owner policy; log values must still be redacted.

This patch does not change runtime semantics, mailbox layout, QoS, deep sleep, hotpath contract or performance budgets.
