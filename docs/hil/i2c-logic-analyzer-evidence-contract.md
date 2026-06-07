# I2C logic analyzer evidence contract

The default production I2C speed for ESP8266 software-master operation is the
safe 100 kHz profile.  A 400 kHz profile is allowed only after measured evidence
for the complete shared bus and all devices attached during the test.

## Required 100 kHz evidence

Capture and archive a sanitized trace/report showing:

- START,
- address plus ACK,
- raw write,
- repeated START,
- raw read,
- final NACK from the master after the last read byte,
- STOP,
- missing-address NACK followed by STOP/release,
- SDA stuck-low recovery with up to nine SCL pulses and final STOP,
- SCL held-low or stretch-timeout handling without a firmware hang.

## Required 400 kHz evidence

The same sequence is required at 400 kHz, plus rise/fall-time notes.  The board
has external pull-ups and a shared motherboard I2C bus; bus capacitance changes
when I2C OUT wiring or modules are attached.  Therefore 400 kHz is not a default
configuration and must not be enabled by profile policy without attached HIL
logic-analyzer evidence.  `EV_ESP8266_I2C_TARGET_SPEED_HZ` is a requested
upper-bound target: the software master must use ceil half-period arithmetic, so
400 kHz requests use a conservative nominal `2 us` half-period until a measured
`measured_hz` proves the actual waveform.

## Firmware/parser markers

Firmware HIL may report bus-completion facts, but it cannot replace a waveform.
The following markers are required for a firmware PASS:

```text
EV_HIL_I2C_ACK_EVIDENCE ...
EV_HIL_I2C_NACK_EVIDENCE ...
EV_HIL_I2C_FINAL_NACK_SENT ...
EV_HIL_I2C_STOP_RELEASE ... sda=1 scl=1 ...
EV_HIL_I2C_RECOVERY_EVIDENCE case=<case> pulses=<0..9> ...
EV_HIL_I2C_CLOCK_STRETCH_EVIDENCE ...
EV_HIL_I2C_MUTEX_EVIDENCE ... unbalanced=0
EV_HIL_I2C_SCAN_NACK_POLICY addr7=0x.. status=NACK recovery_delta=0 stop_release_ok=1
```

Logic analyzer artifacts remain external or private evidence until sanitized.
Public/redacted reports may include only measurements and `<REDACTED>` tokens,
never private WiFi credentials or raw sensitive transcripts.

## Phase 8 real analyzer import contract

A parser self-test is never a real hardware pass.  A real I2C logic-analyzer PASS
requires a manifest/capture bundle and a matching serial transcript that prove:

```text
EV_HIL_BOARD_PIN_MAP board=<name> i2c_port=0 scl_gpio=<n> sda_gpio=<n> onewire_gpio=<n> source=bsp/<board>/pins.def
EV_HIL_I2C_ACK_EVIDENCE ...
EV_HIL_I2C_NACK_EVIDENCE ...
EV_HIL_I2C_FINAL_NACK_SENT ...
EV_HIL_I2C_STOP_RELEASE ... sda=1 scl=1 fail=0
EV_HIL_I2C_BUS_IDLE_AFTER_FAULT ... sda=1 scl=1
EV_HIL_I2C_RECOVERY_EVIDENCE ... pulses=<0..9> stop_attempted=1
EV_HIL_RESULT PASS failures=0
```

For `100 kHz` default captures, the manifest must include START, address ACK,
write, repeated START, read, final NACK, STOP, missing-address NACK + STOP/release,
SDA stuck-low recovery, and SCL held-low timeout or clock-stretch timeout.

For `400 kHz` evidence-gated captures, the same cases are required plus
`measured_hz`, `rise_time_ns`, `fall_time_ns`, pull-up value and capacitance risk
notes.  Without a physical capture/manifest, the gate reports `ENVIRONMENT_BLOCKED`,
not PASS.

## Phase 8 real-analyzer manifest contract

`make hil-logic-analyzer-readiness-gate` is a readiness/import gate.  It may
accept a complete manifest schema, but it must not be treated as real hardware
PASS unless the corresponding serial HIL transcript and analyzer capture are
available in the lab evidence bundle.

A 100 kHz default I2C manifest must list captures for:

```text
start_condition
address_ack
write_byte
repeated_start
read_byte
final_nack_then_stop
stop_after_address_nack
missing_address_nack_stop_release
sda_stuck_low_recovery
scl_held_low_timeout
```

A 400 kHz evidence-gated manifest must additionally include at least one
`fast_400khz*` capture and either the capture or manifest must state:

```text
measured_hz
rise_time_ns
fall_time_ns
pullup_ohms
bus_capacitance_note
```

This keeps 100 kHz as the production default on the shared ATNEL I2C bus and
prevents a nominal 400 kHz mode from being accepted without measured edge timing.
