# adafruit_feather_huzzah_esp8266 USB-UART profile

This directory currently contains host-side USB-UART metadata only. It is not yet
an SDK build target because this repository snapshot does not include a target
`Makefile`/`main/` project for the Adafruit Feather HUZZAH ESP8266.

The profile is kept here so `tools/fw target-usb-profile-check` can validate the
lab metadata for the BSP without leaking USB-host information into firmware BSP
runtime headers.
