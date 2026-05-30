# ESP8266 linker-map memory budget plan

Host `make memory-budget` checks static host-side structure budgets. Production
ESP8266 memory budget requires SDK build outputs and an application ELF section
report from `xtensa-lx106-elf-size -A`.

Per-target SDK memory status is one of `PASS`, `FAIL`, `NOT_RUN`,
`ENVIRONMENT_BLOCKED` or `NOT_APPLICABLE`. PASS requires real EV_MEM evidence
from build artifacts and configured thresholds.

## Release tooling

`config/sdk_memory_budgets.def` defines per-target release thresholds. Use:

```sh
./tools/fw sdk-memory-matrix
make sdk-memory-matrix
```

`PASS` requires EV_MEM markers from an SDK ELF section report. Without SDK build
outputs, targets are reported as `NOT_RUN`, not PASS.

## Strict release mode

The default matrix is report-only because host CI may not have SDK build logs.
Release jobs that have built SDK targets should opt into strict mode:

```sh
EV_SDK_MEMORY_REQUIRE_PASS=1 make sdk-memory-matrix
make sdk-memory-release-gate
```

Strict mode fails if any non-metadata target has no PASS evidence. It is therefore
expected to fail on a fresh source archive with no `logs/sdk/<target>/build.log`
files containing `EV_MEM_*` markers.

## Budget dimensions

The current release matrix covers:

| Dimension | Source | Budget field | Gate behavior |
|---|---|---|---|
| IRAM | ELF section report | `max_iram_bytes` | PASS/FAIL |
| DRAM | ELF section report | `max_dram_bytes` | PASS/FAIL |
| BSS | ELF section report | `max_bss_bytes` | PASS/FAIL |
| DATA | ELF section report | `max_data_bytes` | PASS/FAIL |
| Application `.bin` | application binary matching the application ELF basename | `max_app_bin_bytes` | PASS/FAIL when evidence exists; missing evidence is not PASS |
| Stack frame baseline | GCC `.su` files | none yet | report-only max per-function frame |

The stack baseline is intentionally conservative in language: it is a maximum
single function frame observed in `.su` files, not a whole-program call-chain
proof.
