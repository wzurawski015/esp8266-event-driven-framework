# ATNEL AIR ESP Motherboard WiFi reconnect HIL target

Dedicated hardware-in-the-loop firmware for validating the ESP8266 physical
WiFi adapter behind the bounded HSHA Network Airlock.

Build/run through the wrapper:

```sh
./tools/fw hil-wifi-build
./tools/fw hil-wifi-flash
./tools/fw hil-wifi-monitor
./tools/fw hil-wifi-run
```

The test is intentionally strict. The final serial log must contain exactly:

```text
EV_HIL_RESULT PASS failures=0 skipped=0
```

A skipped phase is a failed qualification.  The operator must provide a
controllable WiFi AP/router using the credentials from the ATNEL board profile.
During the AP-loss phase, disable or move the AP out of range; during the
recovery phase, restore it.

This HIL validates WiFi association/reconnect only. MQTT connectivity,
telemetry, retained state, and remote commands are out of scope.
