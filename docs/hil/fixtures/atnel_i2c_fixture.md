# ATNEL I2C HIL fixture

Target: `atnel_air_esp_motherboard_i2c_hil`.

PASS requires flash plus serial monitor marker:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

## Required base wiring

The ATNEL I2C HIL target validates the primary motherboard I2C bus:

| Signal | Board pin | ESP8266 GPIO | Source |
|---|---|---:|---|
| SCL | `PIN_I2C0_SCL` | GPIO4 | `bsp/atnel_air_esp_motherboard/pins.def` |
| SDA | `PIN_I2C0_SDA` | GPIO5 | `bsp/atnel_air_esp_motherboard/pins.def` |

## Required fault-injection coupling

The fault-injection gates are release HIL gates, not optional smoke checks. The
tracked HIL target uses these fixture pins from `main/component.mk`:

| Fixture function | Fault GPIO | Must couple to | Requirement |
|---|---:|---|---|
| SDA stuck-low containment | GPIO12 | SDA / GPIO5 | Open-drain-safe or series-resistor-limited low drive |
| SCL held-low timeout | GPIO13 | SCL / GPIO4 | Open-drain-safe or series-resistor-limited low drive |
| IRQ flood | GPIO15 | IRQ input GPIO14 | High-impedance during reset; GPIO15 is bootstrap-sensitive |

Checklist before claiming HIL PASS:

- Confirm common ground between the DUT and the fixture driver.
- Confirm GPIO12 can pull SDA/GPIO5 low during the fault window.
- Confirm GPIO13 can pull SCL/GPIO4 low during the fault window.
- Confirm fixture outputs are not push-pull-shorting the I2C bus.
- Confirm the final serial log contains `EV_HIL_RESULT PASS failures=0 skipped=0`.

## Current known failure classification

The run captured in `docs/release/hil_evidence/i2c/20260501T155505Z_excerpt.md`
shows that base I2C gates passed, but `sda-stuck-low-containment` failed with
`stuck_status=OK` and `recovery_status=OK`. That symptom is consistent with an
incomplete SDA fault-injection coupling or a fixture that did not pull SDA/GPIO5
low during the test window. It is not evidence of a general I2C transaction
failure.
