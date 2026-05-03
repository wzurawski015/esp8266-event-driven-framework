# ATNEL AIR ESP Motherboard I2C zero-heap HIL target

Dedicated hardware-in-the-loop firmware for validating the ESP8266 zero-heap
software I2C master on the ATNEL AIR ESP Motherboard wiring.  This target runs
only the HIL suite and then idles; it does not start the normal event-driven
runtime application.

Build/run through the wrapper:

```sh
./tools/fw hil-i2c-build
./tools/fw hil-i2c-flash
./tools/fw hil-i2c-monitor
./tools/fw hil-i2c-run
```

The target is strict: every HIL gate must pass and the final serial log must
contain:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

## Fixture wiring

The destructive/fault-injection tests require an external open-drain-safe
fixture.  The default ATNEL HIL lab profile in `main/component.mk` expects:

| Function | ESP8266 GPIO | External connection |
|---|---:|---|
| SDA stuck-low injection | GPIO12 | through >=1 kOhm or open-drain buffer to SDA/GPIO5 |
| SCL held-low injection | GPIO13 | through >=1 kOhm or open-drain buffer to SCL/GPIO4 |
| IRQ flood output | GPIO15 | through >=1 kOhm or open-drain buffer to IRQ input GPIO14 |

GPIO15 is bootstrap-sensitive.  Keep the fixture high-impedance during reset and
programming.  Do not connect push-pull outputs directly to SDA, SCL, or IRQ
lines.

Override `EV_BOARD_HIL_I2C_SDA_FAULT_GPIO`,
`EV_BOARD_HIL_I2C_SCL_FAULT_GPIO`, and `EV_BOARD_HIL_IRQ_FLOOD_OUTPUT_GPIO` only
when those fixture wires are physically installed.

## Fault-injection diagnostics

The HIL firmware logs fault-fixture levels before, during and after the
stuck-low window. For the tracked profile the expected coupling is:

```text
sda_fault_gpio=12 -> SDA/GPIO5
scl_fault_gpio=13 -> SCL/GPIO4
```

A line such as:

```text
fault-fixture:sda-stuck-low-containment:during_fault fault_gpio=12 fault_level=0 sda_gpio=5 sda_level=1
```

means the fault output went low but the real SDA bus line did not. In that case
the expected failure reason is `fault GPIO did not pull SDA low; check fixture
coupling`; do not treat it as a firmware PASS.

## Fault-fixture diagnostic note

Successful OLED/RTC/MCP23008 traffic confirms the base I2C bus. It does not prove that the GPIO12 fault fixture pulls SDA/GPIO5 low. The fault-fixture log must show `sda_gpio=5` and `scl_gpio=4`; the `sda-stuck-low-containment` case remains FAIL until the serial log reports `EV_HIL_RESULT PASS failures=0 skipped=0`.
