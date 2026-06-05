# OneWire DS18B20 HIL contract

ATNEL OneWire HIL is a separate evidence stream from I2C.  A parser self-test can
prove parser behavior, but only a real serial transcript can produce a hardware
PASS.

A real PASS requires all of these markers.  The detailed `EV_HIL_ONEWIRE_TIMING ... wifi=on` marker and the explicit `EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on` marker are separate requirements; either one alone is insufficient.

```text
EV_HIL_ONEWIRE_PIN_MAP ... dq_gpio=<n> pullup=external ... wifi=on
EV_HIL_ONEWIRE_TIMING ... reset_low_us=<n> presence_low_us=<n> slot_min_us=<n> slot_max_us=<n> wifi=on
EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on
EV_HIL_ONEWIRE_PIN_MAP board=<board> dq_gpio=<n> pullup=external required=1 ... wifi=on
EV_HIL_ONEWIRE_TIMING reset_low_us=<n> presence_low_us=<n> slot_min_us=<n> slot_max_us=<n> ... wifi=on
EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on
EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC name=ds18b20-read-irq-flood ... status=PASS
EV_HIL_ONEWIRE_RELEASE_EVIDENCE name=ds18b20-read-irq-flood dq=1 busy=0 bus_errors_delta=0 ...
EV_HIL_CASE ds18b20-read-irq-flood PASS
EV_HIL_STACK task=irq-flood ...
HIL summary passed=<n> failed=0 skipped=0
EV_HIL_RESULT PASS failures=0 skipped=0
```

The parser also requires before/after IRQ and OneWire diagnostics.  Missing
hardware, placeholder paths, absent serial logs, WiFi disabled, or WiFi-on timing
that cannot be measured are reported as `ENVIRONMENT_BLOCKED` or FAIL, not PASS.

The electrical evidence focus is different from I2C: DS18B20 reset/presence,
scratchpad CRC, DQ release, timing critical-section budgets, WiFi-on timing
truth and non-blocking conversion wait are the core risks.  A `wifi=off` marker
is diagnostic only; it is never sufficient for real hardware PASS.
