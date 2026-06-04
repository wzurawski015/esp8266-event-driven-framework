# I2C SDK-bug avoidance report

| Field | Status |
|---|---|
| SDK command-link runtime use | Forbidden |
| ESP8266 I2C runtime path | GPIO open-drain software master |
| Runtime heap allocation | Forbidden |
| Boot-time mutex exception | Allowed and documented |
| ACK/NACK completion policy | STOP/release or bounded fault |
| Stuck-line recovery | Required |
| New sensors added | No |

This report documents the boundary before adding BME280/BH1750/DS18B20 extensions. The purpose is to keep the foundation independent from the known ESP8266 SDK I2C command-link failure class represented by `tools/patches/0001-esp8266-i2c-stop-and-speed-fix.patch`.
