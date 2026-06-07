# Logic-analyzer capture manifest schema

This document defines the minimal metadata required before an I2C or OneWire HIL
run can be treated as real waveform-backed evidence. Parser self-tests and
firmware serial markers are not enough for `REAL_HIL_PASS`.

A manifest is a sanitized JSON file. It may reference private capture files, but
it must not contain WiFi SSID, passwords, command tokens, or raw serial content.

Required common fields:

```json
{
  "suite": "i2c",
  "board": "atnel_air_esp_motherboard",
  "run_id": "20260605T000000Z",
  "operator": "<REDACTED>",
  "firmware_commit": "abcdef0",
  "logic_analyzer_model": "model-name",
  "sample_rate_hz": 24000000,
  "channels": {"scl": 0, "sda": 1},
  "captures": [{"name": "stop_after_address_nack"}]
}
```

For `suite=i2c`, required captures are:

```text
stop_after_address_nack
final_nack_then_stop
sda_stuck_low_recovery
```

The I2C serial transcript must still contain firmware markers such as
`EV_HIL_BOARD_PIN_MAP`, `EV_HIL_I2C_ACK_EVIDENCE`,
`EV_HIL_I2C_NACK_EVIDENCE`, `EV_HIL_I2C_FINAL_NACK_SENT`,
`EV_HIL_I2C_STOP_RELEASE sda=1 scl=1 fail=0`,
`EV_HIL_I2C_RECOVERY_EVIDENCE`, and `EV_HIL_RESULT PASS failures=0`.

For `suite=onewire`, required fields include `"wifi_state": "on"`, channel
`dq`, and captures:

```text
onewire_reset_presence
onewire_read_write_slots_wifi_on
```

The OneWire serial transcript must still contain
`EV_HIL_ONEWIRE_PIN_MAP ... wifi=on`, `EV_HIL_ONEWIRE_TIMING ... wifi=on`,
`EV_HIL_ONEWIRE_WIFI_TIMING status=PASS wifi=on`,
`EV_HIL_ONEWIRE_DS18B20_SCRATCHPAD_CRC ... status=PASS`,
`EV_HIL_ONEWIRE_RELEASE_EVIDENCE ... dq=1 busy=0 bus_errors_delta=0`, and
`EV_HIL_RESULT PASS failures=0 skipped=0`.

No manifest alone is a hardware PASS. `make hil-logic-analyzer-readiness-gate`
returns `ENVIRONMENT_BLOCKED` when no real manifest path is supplied.
