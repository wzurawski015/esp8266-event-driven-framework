# Network backpressure

The network outbox is bounded and category-aware:

- telemetry latest,
- telemetry periodic,
- command response,
- critical fault.

When disconnected or congested, network code must not grow unbounded buffers. Policy controls whether a category rejects new data or drops the oldest entry.
