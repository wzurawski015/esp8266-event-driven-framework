# ATNEL AIR ESP Motherboard OneWire IRQ HIL target

Dedicated hardware-in-the-loop firmware for validating the ESP8266 bit-banged
1-Wire slow path while a GPIO IRQ flood is active.  This target is intentionally
separate from the I2C HIL target because the default I2C fault fixture may reuse
GPIO12, which is also the ATNEL 1-Wire DQ line.

Build/run through the wrapper:

```sh
./tools/fw hil-onewire-build
./tools/fw hil-onewire-flash
./tools/fw hil-onewire-monitor
./tools/fw hil-onewire-run
```

The target is strict: the final serial log must contain:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

## Fixture wiring

| Function | ESP8266 GPIO | External connection |
|---|---:|---|
| DS18B20 DQ | GPIO12 | normal ATNEL 1-Wire sensor bus |
| IRQ flood output | GPIO15 | through >=1 kOhm or open-drain buffer to IRQ input GPIO14 |

GPIO15 is bootstrap-sensitive. Keep the fixture high-impedance during reset and
programming. Do not connect push-pull outputs directly to IRQ lines.
