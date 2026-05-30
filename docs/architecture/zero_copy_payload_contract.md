# Zero-copy payload contract

The event framework supports four semantic payload kinds from `config/events.def`:

| Payload kind | Ownership | Copy policy | Release policy | Typical use |
|---|---|---|---|---|
| `EV_PAYLOAD_INLINE` | envelope owns bytes | copied into `EV_MSG_INLINE_CAPACITY` | no release | ticks, small commands |
| `EV_PAYLOAD_COPY_FIXED` | envelope owns bounded metadata bytes | copied into `EV_MSG_INLINE_CAPACITY` | no release | faults, lifecycle records |
| `EV_PAYLOAD_LEASE` | lease pool owns backing storage | no payload-byte copy in fan-out | every queued envelope retains/releases one share | stream chunks, MQTT leased RX, OLED frames |
| `EV_PAYLOAD_STREAM_VIEW` | producer owns backing storage | no payload-byte copy | framework never releases | borrowed stream windows |

## Lease rules

A non-empty lease payload must provide both retain and release callbacks before the payload is attached to an `ev_msg_t`. Fan-out works by copying only the envelope metadata and retaining one additional share per mailbox slot.

A caller that acquired the original lease handle keeps responsibility for releasing that original handle. Message disposal releases only the message-owned share. Queue pop transfers the queued envelope to the consumer, and actor runtime disposal releases the queued share after the handler returns.

## Stream-view rules

A stream view is borrowed. It must not provide retain or release callbacks, and the framework must not attempt to free or release it. The producer must ensure that the backing storage outlives all consumers that see the view. Because no current production event is declared as `EV_PAYLOAD_STREAM_VIEW`, this contract is enforced in the message layer and documented for future stream sources.

## Failure rules

- Attaching inline bytes to a lease event is a contract error.
- Attaching a non-empty lease without retain/release callbacks is a contract error.
- Attaching ownership callbacks to a stream-view event is a contract error.
- Pool exhaustion returns `EV_ERR_FULL`; it must not allocate from heap.
- Double-release or stale lease handles return `EV_ERR_STATE` and update stale-handle diagnostics.
