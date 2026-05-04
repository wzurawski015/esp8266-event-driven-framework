# ESP8266 linker-map memory budget plan

Host `make memory-budget` checks static host-side structure budgets. Production
ESP8266 memory budget requires SDK build outputs and an application ELF section
report from `xtensa-lx106-elf-size -A`.

Per-target SDK memory status is one of `PASS`, `FAIL`, `NOT_RUN`,
`ENVIRONMENT_BLOCKED` or `NOT_APPLICABLE`. PASS requires a real ELF/section
report and configured thresholds.

## Release tooling

`config/sdk_memory_budgets.def` defines per-target release thresholds. Use:

```sh
./tools/fw sdk-memory-matrix
make sdk-memory-matrix
```

`PASS` requires EV_MEM markers from an SDK ELF section report. Without SDK build
outputs, targets are reported as `NOT_RUN`, not PASS.
