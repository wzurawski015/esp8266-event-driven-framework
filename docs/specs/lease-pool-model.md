# Lease Pool Model

## Purpose

The framework needs a deterministic way to carry shared payload ownership without heap allocation in hot paths.
The lease pool provides that mechanism as a fixed-slot, fixed-capacity allocator with explicit retain/release accounting.

## Core contract

- the pool owns all backing storage,
- callers acquire one slot and receive one handle with one reference,
- retains and releases are explicit and measurable,
- a slot returns to the free set only when its reference count reaches zero,
- pool-backed message attachment retains a separate share for the message envelope.

## Why this exists

Raw callback-based retain/release hooks are flexible, but they are not self-describing enough for a long-lived embedded framework.
A first-class lease pool gives us:

- deterministic capacity,
- explicit pressure metrics,
- handle-based reviewability,
- a natural bridge to future stream/zero-copy backends.

## Fixed-slot structure

The current contract-stage pool is intentionally simple:

- all slots have the same capacity,
- all storage is caller-provided,
- no runtime heap allocation occurs,
- slot metadata is caller-provided and statically sized.

This keeps the contract auditable and easy to test on host builds.

## Ownership rules

### Acquired handle

A successful `ev_lease_pool_acquire()` returns a handle that owns one reference.
The caller must eventually release that handle unless ownership has been moved into another explicit contract.

### Message attachment

`ev_lease_pool_attach_msg()` retains one additional pool reference for the message.
That means:

- the caller still owns its original handle,
- the message owns a second share,
- mailbox enqueue or publish fan-out may retain additional shares later,
- every retained share must have a matching release path.

### Mailbox transport

When a pool-backed lease message is queued:

- the queue retains one additional share for the queued envelope,
- dropping, overwriting, resetting, or disposing that queued envelope releases the queue-owned share.

## Failure-path rule

If message attachment fails after retaining a message-owned share, the helper must roll back that retain before returning an error.
No partial ownership transfer is allowed.

## Current limitations

The current pool is deliberately minimal:

- fixed slot size,
- no variable-size classes,
- no cross-pool move semantics,
- no ISR-specific API yet,
- no stream chunk reservations yet.

Those are planned future extensions, not gaps in the current contract.
