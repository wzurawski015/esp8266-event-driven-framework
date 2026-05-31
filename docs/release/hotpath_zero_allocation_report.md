# Hot-path zero-allocation report

## Scope

This report covers the ESP8266 portable event-engine hot path, not Linux/GNSS transport code. The source of truth is `config/hotpath_contract.def`.

## What changed

- Added a machine-readable hot-path manifest.
- Added `tools/audit/hotpath_zero_alloc_contract.py`.
- Added `make hotpath-zero-alloc-gate`.
- Added explicit message payload contract helpers.
- Added a host test for zero-copy lease fan-out and bounded pool exhaustion.
- Tightened stream-view semantics so borrowed views cannot carry ownership callbacks.
- Hardened mailbox store-failure cleanup after a retained queue share.

## Allocation contract

The hot path forbids heap allocation and deallocation APIs. The gate scans the manifest-listed files after stripping C comments and fails on heap, blocking primitives, SDK includes and stdio/log I/O.

## Copy budget

Allowed copies are explicit: inline payload copy into `EV_MSG_INLINE_CAPACITY`, fixed message-envelope metadata copies, and deterministic lease-pool slot clearing/metadata handling. Large payload fan-out must use `EV_PAYLOAD_LEASE`. The new host test verifies that two mailbox queues receive the same payload pointer and that every queued envelope owns/releases its own retain share.

## RAM and hot-path impact

No mailbox capacity, route table, actor placement, runtime graph storage or scheduler semantics changed. The runtime footprint remains governed by exact mailbox layout and opaque graph storage. The new audit is host-only and has no target firmware runtime cost.

## Validation target

```sh
make hotpath-zero-alloc-gate
```

For full hardening:

```sh
make safety-gate
make quality-gate
make perf-gate
```
