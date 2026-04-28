# ev_platform component

Reusable ESP8266 RTOS SDK-backed implementations of the public framework
platform contracts.

Current scope of this component:

- monotonic clock adapter,
- log sink adapter,
- reset/restart adapter,
- UART adapter,
- shared boot/diagnostic harness used by the generic ESP8266 target,
- ATNEL-facing runtime wrapper that boots the first real cooperative demo application.

This component is intentionally narrow.
It exists to prove the `ports/` contracts on target hardware before wider BSP
peripheral work begins.

Behavioral guarantees in the current stage:

- the UART adapter currently supports UART0 only and rejects other port numbers with `EV_ERR_UNSUPPORTED`,
- invalid UART settings are rejected explicitly instead of being silently coerced,
- log flush is best-effort and does not depend on UART driver installation order,
- wall-clock time is still intentionally unsupported,
- the public monotonic clock stays 64-bit in microseconds while the current ESP8266 implementation provides an effective 1 ms resolution,
- the current monotonic source is derived from a 32-bit millisecond counter and therefore wraps after roughly 49.7 days of uptime,
- USB modem-control boot/reset choreography stays an operator workflow concern and is not part of the public UART contract,
- diagnostic targets may project monotonic microseconds into a 32-bit millisecond log view when serial portability requires it,
- generic and board-scoped targets should reuse this component rather than reimplement target-local boot/diagnostic loops.
