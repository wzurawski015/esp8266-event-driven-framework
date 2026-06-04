# OneWire DS18B20 HIL contract

ATNEL OneWire HIL is a separate evidence stream from I2C.  A parser self-test can
prove parser behavior, but only a real serial transcript can produce a hardware
PASS.

A real PASS requires all of these markers:

```text
EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC name=ds18b20-read-irq-flood ... status=PASS
EV_HIL_ONEWIRE_RELEASE_EVIDENCE name=ds18b20-read-irq-flood dq=1 busy=0 bus_errors_delta=0 ...
EV_HIL_CASE ds18b20-read-irq-flood PASS
EV_HIL_STACK task=irq-flood ...
HIL summary passed=<n> failed=0 skipped=0
EV_HIL_RESULT PASS failures=0 skipped=0
```

The parser also requires before/after IRQ and OneWire diagnostics.  Missing
hardware, placeholder paths and absent serial logs are reported as
`ENVIRONMENT_BLOCKED`, not PASS.

The electrical evidence focus is different from I2C: DS18B20 reset/presence,
scratchpad CRC, DQ release, timing critical-section budgets and non-blocking
conversion wait are the core risks.
