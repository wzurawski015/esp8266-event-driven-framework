# Wemos one-shot evidence contract

The Wemos one-shot evidence workflow is a release evidence boundary around existing runtime, SDK and HIL tools. It does not change runtime semantics, QoS, mailbox layout, actor placement, graph storage, hot-path contract or performance budgets.

The workflow is a state machine:

```text
PREFLIGHT -> SECRETS_STATUS -> DISTCLEAN -> DEFCONFIG -> BUILD -> SIZE_MAP_STACK -> FLASH -> SERIAL_MONITOR -> PARSE_FLASH -> PARSE_SMOKE -> REPORT -> GATE
```

Flash and serial monitoring require explicit operator intent. Missing hardware or consent is `ENVIRONMENT_BLOCKED`, not PASS. The workflow records SHA-256 for evidence artifacts and writes a machine-readable `manifest.json`.

## Read-only gates

Evidence gates are read-only validators. A gate must not mutate tracked release reports or current parsed JSON. Report generation requires an explicit report/update mode and must be backed by a committed `manifest.json`.

A Wemos one-shot `PASS_*` report is invalid if it points to `test-run`, `/tmp`, a temporary directory, or `build/selftest`.
