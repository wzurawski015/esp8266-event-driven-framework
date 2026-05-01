# SDK build matrix

This file defines the release policy for ESP8266 SDK target builds. The source of
truth for target classification is `config/sdk_targets.def` after this iteration.

| Class | Meaning |
|---|---|
| `buildable_sdk` | SDK project buildable without hardware. |
| `hil_sdk` | SDK project buildable without hardware; flash/monitor requires fixture. |
| `physical_smoke` | SDK project buildable; release smoke requires physical board. |
| `metadata_only` | USB-UART/BSP metadata only; no SDK build expected. |

SDK build results must be reported as `PASS`, `FAIL`, `NOT_RUN`,
`ENVIRONMENT_BLOCKED` or `NOT_APPLICABLE`.

## Tools

Use:

```sh
./tools/fw sdk-target-list
./tools/fw sdk-matrix-check
./tools/fw sdk-build-one esp8266_generic_dev
./tools/fw sdk-build-matrix
./tools/fw sdk-build-matrix-report
```

`./tools/fw sdk-build-matrix-report` is allowed to generate `NOT_RUN` rows when
SDK execution is not available. It must not report PASS without a completed build.

## CI policy update

SDK build matrix validation is defined in `.github/workflows/sdk-matrix.yml`.
Hardware HIL is defined in `.github/workflows/hil-self-hosted.yml` and requires
manual `workflow_dispatch`, a self-hosted `esp8266-hil` runner, a serial port and
explicit hardware confirmation. GitHub-hosted CI must not report HIL PASS.
