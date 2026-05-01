# Final release validation summary

Status values are `PASS`, `FAIL`, `NOT_RUN`, `ENVIRONMENT_BLOCKED` and `NOT_APPLICABLE`.
This summary must not collapse `NOT_RUN` into `PASS`.

| Area | Status | Evidence |
|---|---:|---|
| Host quality gate | PASS | User-provided validation log and current host gates. |
| Docs/release gate | PASS | User-provided validation log contains release-gate passed. |
| Static contracts | PASS | Validated by host static-contracts gate. |
| Routegen/docgen freshness | PASS | routegen/docgen are host gates; rerun before release. |
| SDK toolchain check | NOT_RUN | No SDK toolchain log was provided in this patch build. |
| SDK build matrix: buildable targets | PASS | Buildable/physical-smoke SDK targets from docs/release/sdk_build_matrix_report.md. |
| SDK build matrix: HIL SDK targets | PASS | HIL SDK targets are separate from non-HIL SDK build matrix. |
| SDK linker-map memory matrix: buildable targets | PASS | Buildable/physical-smoke memory rows from docs/release/sdk_memory_matrix_report.md. |
| SDK linker-map memory matrix: HIL SDK targets | PASS | HIL SDK memory rows are separate from non-HIL memory matrix. |
| ATNEL I2C HIL | FAIL | Current failure is isolated to sda-stuck-low-containment fixture/fault-injection coupling. |
| ATNEL OneWire HIL | NOT_RUN | Requires physical fixture and serial PASS marker. |
| ATNEL WiFi HIL | NOT_RUN | Requires physical fixture and serial PASS marker. |
| Wemos minimal runtime smoke | NOT_RUN | Requires physical board and marker-based or runtime-alive-fallback PASS. |
| Wemos board constraints | PASS | Constrained to minimal runtime and 2 MB default flash. |

## Remaining production work

- Resolve ATNEL I2C `sda-stuck-low-containment` fixture/fault-injection failure.
- Run ATNEL OneWire HIL on a physical fixture and archive the serial PASS/FAIL log.
- Run ATNEL WiFi HIL on a physical fixture and archive the serial PASS/FAIL log.
- Run Wemos minimal-runtime smoke to completion and archive the serial log.
- Archive hardware logs as release artifacts; local `logs/` remains ignored by Git.
