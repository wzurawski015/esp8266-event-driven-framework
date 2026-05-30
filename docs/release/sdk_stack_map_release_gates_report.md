# SDK stack/map release gates report

## Baseline finding

The SDK memory budget configuration already contains `max_app_bin_bytes`, but the
previous matrix only evaluated IRAM, DRAM, BSS and DATA. This meant application
binary size was configured but not enforced by the matrix.

Stack usage reporting previously emitted only a compatibility placeholder and did
not parse GCC `.su` files.

## Patch scope

This patch strengthens release engineering without changing firmware runtime
semantics:

- `tools/sdk_memory_report.py` emits `EV_MEM_APP_BIN` when the application `.bin`
  can be matched deterministically to the application ELF basename.
- `tools/sdk_memory_report.py` emits `EV_MEM_STACK_USAGE` as a report-only GCC
  `.su` per-function max-frame baseline.
- `tools/sdk_memory_matrix.py` parses and gates `APP_BIN` against
  `max_app_bin_bytes`.
- `make sdk-memory-release-gate` provides explicit strict mode through
  `EV_SDK_MEMORY_REQUIRE_PASS=1`.

## Non-goals

- No SDK/HIL target behavior is changed.
- No normal `quality-gate` SDK build requirement is added.
- No physical board is required.
- No call-chain worst-case stack proof is claimed.
- No new secrets are added.

## Known exception

The Wemos ESP-WROOM-02 18650 BSP may contain a private, deliberately tracked
`board_secrets.local.h` in this lab repository. That remains a local exception to
normal secret hygiene and is not expanded by this patch.

## Next release-engineering direction

After real SDK matrix jobs archive `EV_MEM_*` logs per target, strict mode can be
used by release automation. A later patch may add an explicit stack-frame budget
field if `.su` generation becomes a standard opt-in build mode.
