# ATNEL I2C SDA stuck-low HIL fixture contract

This fixture is required to close the `sda-stuck-low-containment` HIL case. A PASS requires real hardware evidence; host-only execution must remain `ENVIRONMENT_BLOCKED` or `NOT_RUN`.

## Electrical intent

The fixture must pull the real I2C `SDA` net low through a safe open-drain or series-resistor path. It must not short a push-pull GPIO directly against the bus pull-up or another output.

Required observations:

- `EV_HIL_I2C_SDA_FORCE_LOW requested=1 observed=1`
- `EV_HIL_I2C_BUS_STATE ... during=sda:0,...`
- `EV_HIL_I2C_RECOVERY_RESULT status=OK`
- `EV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS`
- `EV_HIL_RESULT PASS failures=0 skipped=0`

If the forcing GPIO is not electrically coupled to the real `SDA` net, the firmware and parser must report `FIXTURE_NOT_COUPLED`; that status is a FAIL, never a PASS.

## Safe procedure

1. Confirm the ATNEL motherboard target and USB-UART profile.
2. Connect fixture open-drain output to the real `SDA` net.
3. Use a safe pull-down path; avoid direct push-pull contention.
4. Run `make hil-atnel-i2c-evidence` with a real captured serial log or `make hil-atnel-i2c-gate` after capture.
5. Commit only redacted `serial.log`, `parsed.json`, `excerpt.md`, and `sha256sums.txt` evidence under `docs/release/hil_evidence/i2c/<timestamp>/`.
