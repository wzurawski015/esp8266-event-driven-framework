# Trace timestamp monotonic clock report

## Scope

This report covers the `trace: timestamp delivery records from monotonic clock port` hardening step. The patch changes delivery trace timestamping only. It does not change `ev_runtime_graph_t` layout, actor placement, QoS semantics, deep-sleep state transitions, SDK/HIL targets, or application behavior outside trace observability.

## Runtime timestamp policy

Delivery trace records now read monotonic time through the portable `ev_runtime_ports_t.clock` port. The runtime continues to depend only on the portable port contract; it does not include ESP8266 RTOS SDK headers.

`ev_trace_record_t.timestamp_us` remains a 32-bit field in this patch. When the platform clock returns a 64-bit monotonic microsecond value, the trace record stores the low 32 bits. Consumers must compare delivery trace timestamps modulo 2^32.

## Fallback behavior

The delivery trace timestamp is `0U` only on explicit fallback paths:

- the runtime graph pointer is absent,
- the clock port is absent,
- the clock function is absent,
- the clock function returns an error.

A clock-port failure must not block or fail message delivery. Delivery continues and the trace record is still emitted with the fallback timestamp.

## Host coverage

The host test `tests/host/test_delivery_trace_timestamp.c` verifies:

- nonzero timestamps when a fake monotonic clock is installed,
- increasing timestamps across consecutive delivery trace records,
- low-32-bit storage for a 64-bit monotonic timestamp,
- fallback `0U` when no clock port is installed,
- fallback `0U` when the clock port returns an error,
- successful delivery despite a clock-port error.

`tools/audit/static_contracts.py` also includes a static contract that rejects reintroducing direct zero assignment to the delivery trace timestamp in the main trace record path.
