# Layering contract report

Date: 2026-05-25
Scope: documentation and non-invasive audit hardening only.

## Input state

The analysed archive was `esp8266-event-driven-framework_20260525_084728.tar.gz`.
Its SHA-256 was:

```text
175dc1d4b75c866e3456ef2709ade0e8754aeb47dccfa281522e637e771a5a1f
```

Baseline validation before the patch passed for route generation, mailbox layout
freshness, routegen check, static contracts, actor/module descriptor consistency,
memory budget, SDK target matrix metadata, and host tests.

## Current layering observations

- The repository already has the intended macro layers: `config`, `core`,
  `runtime`, `modules`, `drivers`, `ports`, `apps`, `adapters`, `bsp`, `tests`,
  `tools`, and `docs`.
- Portable layers are already protected against ESP8266 SDK include leakage by
  `tools/audit/static_contracts.py`.
- Adapter bootstrap exceptions are explicit and allowlisted.
- Actor/module descriptor consistency and exact mailbox layout are already backed
  by dedicated gates.
- `runtime_graph` is the canonical runtime owner, but its structure is still
  public.

## Named exceptions / violations retained intentionally

- Concrete device actors remain under `core/src` and their public headers remain
  under `core/include/ev`.
- `ev_runtime_graph_t` remains public, so app and test code may still inspect
  fields while the accessor boundary is incomplete.
- `runtime/src/ev_delivery_service.c` still writes delivery trace
  `timestamp_us = 0U`.
- `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h` is privately and
  deliberately tracked. This is a private lab exception to secret hygiene and is
  not suitable for a public repository.

## Changes introduced by this patch

- Added `docs/architecture/layering-contract.md` as the hard reference for layer
  responsibilities, allowed dependencies, forbidden dependencies, SDK visibility,
  BSP visibility, device actor visibility, hot-path participation,
  bootstrap-only status, known exceptions, and migration direction.
- Linked the layering contract from the architecture overview and documentation
  index.
- Updated `docs/specs/static-contracts.md` to separate rules already enforced,
  rules planned, and known temporary exceptions.
- Extended `tools/audit/static_contracts.py` only enough to ensure the layering
  contract exists and contains the required main layer sections.

## Enforcement plan for later commits

1. Keep the written contract stable and visible in CI through the static-contract
   audit.
2. Hide graph internals behind public accessors without changing runtime
   semantics.
3. Replace remaining graph white-box access in apps/adapters/tests with those
   accessors.
4. Enforce route delivery policies end to end.
5. Add publish/runtime-poll microbenchmarks.
6. Move concrete device actors out of `core/` after the graph boundary is stable.
7. Timestamp delivery trace records from the monotonic clock port.

## Recommended next patch

The next safest and most valuable step is:

```text
runtime: hide graph internals behind public accessors
```

That step should be mechanical and incremental: add public accessors, migrate
call sites, keep runtime semantics unchanged, and only then consider stronger
static checks against direct graph-field access.
