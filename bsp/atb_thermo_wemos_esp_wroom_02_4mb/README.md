# ATB THERMO Wemos ESP-WROOM-02 4 MB BSP

This BSP describes the ATB THERMO Rev7 carrier board with a Wemos ESP-WROOM-02
4 MB module. It is intentionally separate from `wemos_esp_wroom_02_18650`, which
remains a minimal runtime smoke target.

## Pin map

| ATB function | Wemos pin | ESP8266 GPIO | Policy |
|---|---:|---:|---|
| I2C SCL | D1 | GPIO5 | 100 kHz default |
| I2C SDA | D2 | GPIO4 | 100 kHz default |
| PWR_CTRL / BOOT | D3 | GPIO0 | boot-sensitive, JP2 2-3 normal mode |
| LED | D4 | GPIO2 | active-low |
| PIR_CHECK | D5 | GPIO14 | input sample only in this phase |
| AUDIO | D6 | GPIO12 | idle output only in this phase |
| 1-Wire DS18B20 | D7 | GPIO13 | external pull-up on ATB board |
| PIR_DIS | D8 | GPIO15 | safe default output |

JP2 must be in the 2-3 position for normal peripheral-power control. GPIO0 is a
boot strap pin and must never be treated as a generic free GPIO.

## Hardware policy

DS18B20 and BH1750 are configured as optional supervised hardware. Missing or
unpopulated sensors must not crash the runtime. BME280/BMP180 and OLED are
scanned as optional/deferred I2C addresses but are not enabled as runtime actors
in this phase.
