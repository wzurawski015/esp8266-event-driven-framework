# ATNEL I2C HIL report

| Field | Value |
|---|---|
| Status | FAIL |
| Scope | Base I2C cases passed; failure is isolated to fault-injection containment. |
| Failed case | `sda-stuck-low-containment` |
| Reason | Base I2C cases passed; `sda-stuck-low-containment` failed with `stuck_status=OK` and `recovery_status=OK`, indicating that the fault-injection GPIO likely did not pull the real SDA line low or the fixture coupling is incomplete. |
| Evidence | `docs/release/hil_evidence/i2c/20260501T155505Z_excerpt.md` |
| Local log path | `logs/hil/i2c/20260501T155505Z.log` |

## Passed base I2C cases in the same run

```text
EV_HIL_CASE rtc-read-1000 PASS
EV_HIL_CASE mcp23008-read-write-1000 PASS
EV_HIL_CASE oled-partial-flush PASS
EV_HIL_CASE oled-full-scene-flush PASS
EV_HIL_CASE missing-device-nack PASS
```

## Fault-injection fixture checklist

- `GPIO12` / `EV_BOARD_HIL_I2C_SDA_FAULT_GPIO` must be physically coupled to `SDA` / `GPIO5` / `PIN_I2C0_SDA`.
- `GPIO13` / `EV_BOARD_HIL_I2C_SCL_FAULT_GPIO` must be physically coupled to `SCL` / `GPIO4` / `PIN_I2C0_SCL`.
- The fixture and DUT must share common ground.
- Coupling must be open-drain-safe or series-resistor limited; do not short a push-pull output directly to SDA/SCL.
- Verify with a logic analyzer or voltmeter that SDA actually drops low during the `sda-stuck-low-containment` fault window.
- Do not rely on PASS until the serial log contains `EV_HIL_RESULT PASS failures=0 skipped=0`.

PASS requires the serial marker `EV_HIL_RESULT PASS failures=0 skipped=0`.
