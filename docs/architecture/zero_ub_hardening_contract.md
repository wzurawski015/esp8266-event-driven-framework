# Zero UB hardening contract

The portable ESP8266 framework is validated through a layered host safety model:
C17 strict warnings-as-errors, ASAN/UBSAN, TSAN, static analysis when a backend is available,
coverage over critical files, and deterministic fuzz/property stress over message, mailbox,
lease, QoS, and power-state contracts.

Missing tools are not converted into PASS. They are reported as `ENVIRONMENT_BLOCKED` unless
the caller explicitly allows blocked optional tools for local developer workflows.

This contract is host-side only and does not change SDK or runtime semantics.
