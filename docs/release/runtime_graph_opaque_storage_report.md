# Runtime graph opaque static storage report

## Decision

`ev_runtime_graph_t` no longer exposes runtime implementation fields in `runtime/include/ev/runtime_graph.h`.
The public type remains stack/static allocatable through an opaque storage union sized by
`EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES`.

The internal implementation layout lives in `runtime/src/ev_runtime_graph_internal.h` as
`ev_runtime_graph_impl_t`. Only `runtime/src` may include that header.

## Why opaque static storage instead of heap

ESP8266 targets require deterministic ownership and bounded memory. The patch does not introduce
`malloc`, RTOS heap allocation, or dynamic graph creation. Application code can still own an
`ev_runtime_graph_t` directly as a global, static object, stack object, or member of a composition root.

## Public API boundary

`runtime_graph.h` keeps the core lifecycle/build/publish/send API and the bounded opaque graph type.
Specialized inspection, timer, and trace accessors moved to narrow public headers:

- `runtime_graph_inspection.h`
- `runtime_graph_timers.h`
- `runtime_graph_trace.h`

## Hidden fields

The public header no longer defines graph fields such as registry, actor runtimes, mailboxes,
message storage, services, metrics, trace ring, active route table, scheduler, ports, board profile,
or capability snapshots.

## RAM impact

After adding the optional BH1750 actor, the internal implementation is 24280 bytes on
the host probe. The public opaque wrapper is 24304 bytes, leaving 24 bytes of
conservative alignment/padding headroom without exposing runtime internals.

Mailbox storage remains exact-layout based: 152 message slots and 10944 bytes.

## Hot path impact

The patch changes API visibility, not delivery semantics. The implementation uses inline internal
helpers/macros to map public opaque storage to the private implementation. No heap allocation, extra
route traversal, or new runtime dispatch is introduced.

## Out of scope

This patch does not move actors out of `core/`, does not change QoS semantics, does not alter mailbox
layout, does not alter active route spans, and does not modify trace timestamp behavior.
