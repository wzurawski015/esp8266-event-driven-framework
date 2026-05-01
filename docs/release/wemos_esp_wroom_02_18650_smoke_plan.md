# Wemos ESP-WROOM-02 18650 minimal-runtime smoke plan

## Board facts used by this release plan

- ESP8266EX / ESP-WROOM-02 module.
- CP2102 USB-UART through microUSB.
- RESET and FLASH buttons.
- GPIO16 / WeMos D0 is the blue user LED and deep-sleep wake pin when tied to RST.
- A0 is analog through a divider; it is not a general-purpose digital GPIO.
- Practical default flash size is 2 MB / 16 Mbit; 4 MB variants must be selected
  explicitly and documented.

## Smoke scope

The Wemos target is minimal-runtime. It does not claim DS18B20, RTC, OLED,
MCP23008, NET or WDT hardware without external wiring and validation.

PASS requires serial smoke markers from the board. If the board is absent, the
status is `NOT_RUN` with reason.

## SDK defaults and bootloader procedure

The tracked release defaults use `CONFIG_ESPTOOLPY_FLASHSIZE_2MB=y`, DIO flash
mode, 40 MHz flash frequency and 115200 console/monitor baud. Manual bootloader
entry is documented for boards where auto-reset is unreliable: hold FLASH, press
and release RESET, release FLASH, then start flashing.

## Smoke commands

```sh
./tools/fw wemos-smoke-build
./tools/fw wemos-smoke-flash
./tools/fw wemos-smoke-flash-manual
./tools/fw wemos-smoke-monitor
./tools/fw wemos-smoke-run
./tools/fw wemos-smoke-not-run-report
```

The firmware emits bounded smoke markers `EV_WEMOS_SMOKE_BOOT` and
`EV_WEMOS_SMOKE_RUNTIME_READY`. PASS requires observing both over serial.

## Post-reset attach fallback

The smoke monitor still prefers `EV_WEMOS_SMOKE_BOOT` plus
`EV_WEMOS_SMOKE_RUNTIME_READY`. If it attaches after reset and misses those early
lines, it may pass by runtime-alive fallback: at least three increasing diag
actor ticks and three increasing app snapshot sequence numbers, with no reset or
failure markers. The generated smoke report records whether PASS came from
`markers` or `runtime_alive_fallback` mode.
