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
logic-analyzer evidence.

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
```

Logic analyzer artifacts remain external or private evidence until sanitized.
Public/redacted reports may include only measurements and `<REDACTED>` tokens,
never private WiFi credentials or raw sensitive transcripts.
