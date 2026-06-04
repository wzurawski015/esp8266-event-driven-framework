# esptool flash evidence excerpt

```text
configSUPPORT_DYNAMIC_ALLOCATION[[:space:]]+1([[:space:]]|$)' "$freertos_config";     rm -rf /tmp/ev-sdk-patches;     git config --system --add safe.directory /opt/esp/ESP8266_RTOS_SDK;     git -C /opt/esp/ESP8266_RTOS_SDK rev-parse --short HEAD
#9 CACHED

#10 [6/7] RUN set -eux;     python3 -m venv /opt/esp/python-env;     /opt/esp/python-env/bin/pip install --upgrade       --retries 5       --timeout 60       pip       wheel;     /opt/esp/python-env/bin/pip install       --retries 5       --timeout 60       "setuptools<82";     /opt/esp/python-env/bin/pip install       --retries 5       --timeout 60       -r /opt/esp/ESP8266_RTOS_SDK/requirements.txt;     /opt/esp/python-env/bin/pip install --upgrade       --retries 5       --timeout 60       "setuptools<82";     /opt/esp/python-env/bin/python3 -c "import pip, serial, setuptools";     /opt/esp/python-env/bin/python3 --version;     /opt/esp/python-env/bin/pip --version
#10 CACHED

#11 [7/7] WORKDIR /work
#11 CACHED

#12 exporting to image
#12 exporting layers done
#12 writing image sha256:db619be4ae74607dccbe3eb1edca4ad6ce1400e0c3a71091693c188cdad09c86 done
#12 naming to docker.io/library/esp8266-event-driven-sdk:local done
#12 DONE 0.0s
note: flash attempt 1/3 on /dev/ttyUSB0 ...
note: live flash output is mirrored below; if the line stalls at Connecting...., the current esptool handshake is genuinely still in progress.
make: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'
Toolchain path: /opt/esp/xtensa-lx106-elf/bin/xtensa-lx106-elf-gcc
Toolchain version: esp-2020r3-49-gd5524c1
Compiler version: 8.4.0
/opt/esp/ESP8266_RTOS_SDK/tools/check_python_dependencies.py:22: UserWarning: pkg_resources is deprecated as an API. See https://setuptools.pypa.io/en/latest/pkg_resources.html. The pkg_resources package is slated for removal as early as 2025-11-30. Refrain from using this package or pin to Setuptools<81.
  import pkg_resources
Python requirements from /opt/esp/ESP8266_RTOS_SDK/requirements.txt are satisfied.
make[1]: Entering directory '/opt/esp/ESP8266_RTOS_SDK/components/bootloader/subproject'
make[1]: Leaving directory '/opt/esp/ESP8266_RTOS_SDK/components/bootloader/subproject'
make[1]: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650/build/app_update'
App "ev_wroom_02" version: 0bdfa4d
make[1]: Leaving directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650/build/app_update'
Flashing binaries to serial port /dev/ttyUSB0 (app at offset 0x10000)...
esptool.py v2.4.0
Connecting....
Chip is ESP8266EX
Features: WiFi
MAC: 48:3f:da:c5:7c:a8
Uploading stub...
Running stub...
Stub running...
Configuring flash size...
Compressed 10528 bytes to 7098...

Writing at 0x00000000... (100 %)
Wrote 10528 bytes (7098 compressed) at 0x00000000 in 0.6 seconds (effective 134.6 kbit/s)...
Hash of data verified.
Compressed 498368 bytes to 316648...

Writing at 0x00010000... (5 %)
Writing at 0x00014000... (10 %)
Writing at 0x00018000... (15 %)
Writing at 0x0001c000... (20 %)
Writing at 0x00020000... (25 %)
Writing at 0x00024000... (30 %)
Writing at 0x00028000... (35 %)
Writing at 0x0002c000... (40 %)
Writing at 0x00030000... (45 %)
Writing at 0x00034000... (50 %)
Writing at 0x00038000... (55 %)
Writing at 0x0003c000... (60 %)
Writing at 0x00040000... (65 %)
Writing at 0x00044000... (70 %)
Writing at 0x00048000... (75 %)
Writing at 0x0004c000... (80 %)
Writing at 0x00050000... (85 %)
Writing at 0x00054000... (90 %)
Writing at 0x00058000... (95 %)
Writing at 0x0005c000... (100 %)
Wrote 498368 bytes (316648 compressed) at 0x00010000 in 27.9 seconds (effective 142.9 kbit/s)...
Hash of data verified.
Compressed 3072 bytes to 83...

Writing at 0x00008000... (100 %)
Wrote 3072 bytes (83 compressed) at 0x00008000 in 0.0 seconds (effective 1994.9 kbit/s)...
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
make: Leaving directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'

```
