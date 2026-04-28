# Power / Deep Sleep HIL Acceptance

This document defines the hardware acceptance scenarios for the Deep Sleep
integrity path. It is a test plan only; it does not claim that the scenarios have
passed on hardware.

The Deep Sleep transition is accepted only when the firmware can prove that the
runtime is quiescent and the ESP8266 platform can park I/O without leaving the
board in a partially disabled state.

## Required scenarios

1. **Pending IRQ before sleep**
   - Inject a GPIO interrupt sample before `EV_SYS_GOTO_SLEEP_CMD` is processed.
   - Expected result: sleep is rejected, `deep_sleep` is not called, and the IRQ
     sample remains observable by the runtime.

2. **I2C bus held low before sleep**
   - Hold SDA or SCL low using an open-drain fixture or a resistor-protected
     pull-down.
   - Expected result: platform prepare rejects sleep. The adapter may attempt
     bounded recovery, but it must not enter Deep Sleep while the bus is unsafe.

3. **OneWire DQ held low before sleep**
   - Hold the 1-Wire DQ line low using a resistor-protected fixture.
   - Expected result: platform prepare rejects sleep and does not call
     `esp_deep_sleep()`.

4. **Normal quiescent system**
   - Let all actor queues, IRQ samples, I2C transactions and OneWire operations
     drain.
   - Expected result: the log is flushed, I/O is parked, and Deep Sleep is
     entered.

5. **Wake path**
   - Verify that GPIO16/RST wake wiring remains valid for the board profile.
   - Expected result: the board wakes from Deep Sleep using the configured wake
     path.

6. **Sleep current**
   - Measure board current after Deep Sleep entry with all I/O parked.
   - Expected result: current is within the board-specific target documented for
     the selected BSP.

## Notes

- Use open-drain or resistor-protected fixtures for bus fault injection.
- Do not drive ESP8266 bootstrap-sensitive pins during reset/programming.
- UART log visibility is required before final I/O parking; do not interpret a
  missing post-park UART line as a firmware failure unless the pre-park log was
  also missing.
