# Event trace

The trace ring is optional and bounded. Each `ev_trace_record_t` contains:

- timestamp,
- event id,
- source actor,
- target actor,
- result,
- QoS,
- queue depth,
- flags.

The ring records drops instead of allocating memory.
