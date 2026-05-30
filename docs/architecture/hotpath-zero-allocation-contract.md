# ESP8266 hot-path zero-allocation contract

This contract defines the portable ESP8266 event-engine hot path. It is intentionally smaller than the whole application. Actors, demo presentation, adapter bootstrap and BSP code may perform bounded setup work, but the kernel/runtime delivery path must stay deterministic.

## Hot path boundary

The current hot path is named `esp8266_publish_poll_delivery` and is listed in `config/hotpath_contract.def`. It covers message validation, static publish/direct send, mailbox push/pop/dispose, lease-pool retain/release/acquire bookkeeping, active route span lookup, runtime poll/loop delivery, and actor publish/send bridge.

## Hard rules

Within the manifest-listed files:

- no heap allocation or deallocation APIs,
- no blocking primitives,
- no SDK includes,
- no stdio/log I/O,
- no unbudgeted `memcpy`/`memmove` of payload bytes.

Allowed copies are explicit and bounded in the manifest: inline payload copy into `EV_MSG_INLINE_CAPACITY`, fixed message-envelope metadata copies, and deterministic lease-pool slot clearing/metadata handling.

## Payload policy

Small control events may use inline payloads. Large or fan-out payloads must use the zero-copy transport contract. `EV_PAYLOAD_LEASE` is owned, retainable and releasable. `EV_PAYLOAD_STREAM_VIEW` is borrowed and non-owning. `EV_PAYLOAD_COPY_FIXED` is allowed only for bounded fixed-size metadata.

The runtime never allocates heap storage to satisfy fan-out. Queueing a lease-backed payload retains ownership; disposing the queued envelope releases that share.

## Why not absolute zero-copy for every event

On ESP8266, forcing zero-copy for every small event would increase pointer lifetime complexity and often cost more than copying a tiny control payload. The hard invariant is: no heap and no large payload copy in the runtime hot path.
