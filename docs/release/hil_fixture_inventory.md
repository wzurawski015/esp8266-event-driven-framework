# HIL fixture inventory

HIL status is not inferred from host tests. PASS requires a physical fixture,
serial logs and the monitor marker:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

## Fixtures

| Fixture | Target | Required status evidence |
|---|---|---|
| ATNEL I2C | `atnel_air_esp_motherboard_i2c_hil` | Build, flash, monitor log with PASS marker. |
| ATNEL OneWire | `atnel_air_esp_motherboard_onewire_hil` | Build, flash, monitor log with PASS marker. |
| ATNEL WiFi | `atnel_air_esp_motherboard_wifi_hil` | Build, flash, monitor log with PASS marker. |
| Wemos minimal runtime | `wemos_esp_wroom_02_18650` | Build, flash/manual flash, serial smoke markers. |

Without attached hardware the release status is `NOT_RUN`, not PASS.

## CI policy update

SDK build matrix validation is defined in `.github/workflows/sdk-matrix.yml`.
Hardware HIL is defined in `.github/workflows/hil-self-hosted.yml` and requires
manual `workflow_dispatch`, a self-hosted `esp8266-hil` runner, a serial port and
explicit hardware confirmation. GitHub-hosted CI must not report HIL PASS.

## Release runner

Use:

```sh
EV_HIL_RELEASE_MODE=not-run ./tools/fw hil-release-all
EV_HIL_RELEASE_MODE=dry-run ./tools/fw hil-release-dry-run
./tools/fw hil-release-atnel-i2c
./tools/fw hil-release-atnel-onewire
./tools/fw hil-release-atnel-wifi
```

The default release commands run hardware. Use `EV_HIL_RELEASE_MODE=not-run` only
to generate an explicit not-run report.
