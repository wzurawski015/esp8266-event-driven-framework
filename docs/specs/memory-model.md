# Memory Model

## Goals

- deterministic runtime behavior,
- bounded memory consumption,
- explicit ownership,
- no hidden heap traffic in hot paths.

## Planned memory classes

1. **Inline payloads**  
   Small messages that fit directly in an event envelope.

2. **Fixed-copy payloads**  
   Bounded payloads copied into preallocated storage.

3. **Lease-backed payloads**  
   Shared buffers with strict ownership and release semantics.

4. **Stream views**  
   Non-owning descriptors over ring-buffered or DMA-oriented data.

## Enforcement direction

The final framework should make the safe path the default path:

- typed constructors for events,
- mandatory dispose/release paths,
- compile-time catalog checks,
- runtime counters for pool pressure and mailbox overflow.
