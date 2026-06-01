# Evidence log capture workflow

This workflow exists to prevent a common release-evidence failure: using one mixed
terminal transcript as if it were clean SDK, flash, or serial evidence. Mixed
transcripts can contain parser self-tests, memory-report self-tests, flash logs,
serial output, shell prompts and build fragments. They are useful for humans but
not acceptable as machine-verifiable release evidence.

## Required separation

Capture evidence as separate files:

| Evidence kind | File | Required content |
|---|---|---|
| SDK build | `build.log` | `EV_SDK_BUILD_TARGET=<target>` and `EV_SDK_BUILD_STATUS=PASS` for the same target. |
| Size/map | `size.log`, `map_summary.txt` | Non-zero `EV_MEM_*` markers, including non-zero `APP_BIN` for buildable/HIL/physical targets. |
| Stack | `stack_usage.txt` | `EV_MEM_STACK_USAGE ... max_frame=<n>` or explicit `STACK_NOT_AVAILABLE=<reason>`. |
| Flash | `flash.log` | esptool chip detection, write markers, `Hash of data verified.`, and reset/leave marker. |
| HIL serial | `serial.log` | Target-specific HIL or Wemos runtime markers parsed by the dedicated HIL parser. |

Do not use `EV_MEM_REPORT_RESULT PASS` from a self-test as SDK build evidence. It
may validate the memory-report tool, but it does not prove a real target build.

## Example capture commands

```sh
make -C adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650 all \
  2>&1 | tee build.log

make -C adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650 flash \
  2>&1 | tee flash.log

python3 tools/serial_monitor.py --port /dev/ttyUSB0 --baud 115200 --timeout 60 \
  2>&1 | tee serial.log
```

Import each evidence class with its dedicated tool:

```sh
python3 tools/release/import_sdk_evidence.py \
  --import-target wemos_esp_wroom_02_18650 \
  --build-log build.log \
  --size-log size.log \
  --map map_summary.txt \
  --stack stack_usage.txt

python3 tools/release/parse_esptool_flash_log.py \
  --target wemos_esp_wroom_02_18650 \
  --log flash.log

python3 tools/hil/parse_wemos_smoke_log.py --log serial.log
```

Placeholder paths such as `/path/wemos-smoke.log` or `/path/to/build.log` must
produce controlled `ENVIRONMENT_BLOCKED`, never a Python traceback and never a
fake PASS.

## Canonical SDK capture markers

`./tools/fw sdk-build-one <target>` must emit `EV_SDK_BUILD_TARGET=<target>`, `EV_SDK_BUILD_PROJECT=<path>`, `EV_SDK_BUILD_BEGIN`, `EV_SDK_BUILD_STATUS=PASS`, `EV_SDK_BUILD_RC=0` and `EV_SDK_BUILD_END`. Memory-report PASS may validate the memory-report tool, but it does not replace target-specific SDK build proof.
