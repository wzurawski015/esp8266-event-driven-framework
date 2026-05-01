# HIL and Wemos smoke hardening report

Baseline archive: `esp8266-event-driven_20260501_181825.tar.gz`.

## Confirmed current state

- The framework is already after no-legacy hardening: `apps/demo` does not own
  mailbox/runtime/registry/scheduler internals and uses `runtime_graph` /
  `runtime_loop` as the canonical runtime path.
- SDK build matrix reports PASS for buildable SDK targets, HIL SDK targets and
  the Wemos physical-smoke build target. Adafruit Feather HUZZAH remains
  metadata-only / `NOT_APPLICABLE`.
- SDK linker-map memory matrix reports PASS for built SDK targets with real
  IRAM/DRAM/BSS/DATA values.
- ATNEL I2C HIL remains `FAIL`, isolated to `sda-stuck-low-containment`.
- ATNEL OneWire HIL, ATNEL WiFi HIL and Wemos smoke remain `NOT_RUN` unless
  separate physical serial logs prove PASS.

## ATNEL I2C HIL failure classification

Current evidence shows base I2C cases passed:

```text
EV_HIL_CASE rtc-read-1000 PASS
EV_HIL_CASE mcp23008-read-write-1000 PASS
EV_HIL_CASE oled-partial-flush PASS
EV_HIL_CASE oled-full-scene-flush PASS
EV_HIL_CASE missing-device-nack PASS
```

The only captured failure is:

```text
sda-stuck-low-containment stuck_status=OK recovery_status=OK
EV_HIL_CASE sda-stuck-low-containment FAIL
```

The tracked fixture expects `GPIO12` to pull `SDA/GPIO5` low and `GPIO13` to
pull `SCL/GPIO4` low. `stuck_status=OK` during SDA fault injection means the
fault path probably did not force the real bus line low. The failure remains
visible and is not converted to PASS.

## Docs/tooling status

This report is updated by the HIL/smoke hardening iteration. If documentation
build cannot run in an environment because Doxygen/Graphviz are unavailable, the
limitation must be recorded by the operator; docs must not be marked PASS unless
they actually executed.

## Commit 2 I2C fault-injection diagnostics

The I2C HIL firmware now logs fault GPIO and bus GPIO levels before, during and
after stuck-low injection. The expected diagnostic line includes the configured
fault GPIO, the real SDA/SCL bus GPIOs and their sampled levels. If both
`stuck_status` and `recovery_status` are `OK`, the failure reason points to
fixture coupling instead of using a generic containment message.
