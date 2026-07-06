# ATB THERMO Wemos ESP-WROOM-02 4 MB operator guide

This guide describes the dedicated `atb_thermo_wemos_esp_wroom_02_4mb` target.
The existing `wemos_esp_wroom_02_18650` target remains minimal and should not be
used as the ATB THERMO application profile.

## Hardware prerequisites

- ATB THERMO Rev7 board.
- Wemos ESP-WROOM-02 4 MB module.
- JP2 in position 2-3 for normal GPIO0/PWR_CTRL operation.
- USB-UART connected to the Wemos module.

GPIO0/D3 is boot-sensitive. If JP2 is in programming mode or the GPIO0 polarity
has not been verified, do not claim hardware PASS.

## Pin map

| Function | GPIO | Evidence marker |
|---|---:|---|
| SCL | 5 | `EV_ATB_THERMO_PIN_MAP scl=5` |
| SDA | 4 | `EV_ATB_THERMO_PIN_MAP sda=4` |
| DS18B20 DQ | 13 | `EV_ATB_THERMO_ONEWIRE_READY dq=13` |
| PWR_CTRL | 0 | `EV_ATB_THERMO_PWR_CTRL gpio=0` |
| LED | 2 | `EV_ATB_THERMO_LED_READY gpio=2` |
| PIR_CHECK | 14 | `EV_ATB_THERMO_PIR_CHECK gpio=14` |
| AUDIO | 12 | `EV_ATB_THERMO_AUDIO_IDLE gpio=12` |
| PIR_DIS | 15 | `EV_ATB_THERMO_PIR_DIS gpio=15` |

## Build

```sh
./tools/fw sdk-build-one atb_thermo_wemos_esp_wroom_02_4mb 2>&1 | tee build/sdk-atb-thermo.log
python3 tools/audit/sdk_warning_policy.py \
  --project-only \
  --latest-build-session \
  --session-kind build \
  --strict-build-session \
  --target atb_thermo_wemos_esp_wroom_02_4mb \
  build/sdk-atb-thermo.log
```

## Flash and monitor

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atb_thermo_wemos_esp_wroom_02_4mb
export FW_ESPPORT=/dev/ttyUSB0
export FW_MONITOR_BAUD=115200
./tools/fw sdk-flash
./tools/fw sdk-simple-monitor
```

## Where to see readings

Temperature and light are emitted as fixed-point serial markers when the sensors
are present and validated:

```text
EV_DS18B20_TEMP cC=2345 C=23.45
EV_BH1750_LIGHT mLux=12345 lux=12.345
```

MQTT telemetry may also carry the same payloads when the private-lab network
profile is enabled, but UART is the first bring-up source of truth.

## HIL status

This target prints boot and readiness markers, but it does not emit real HIL PASS.
Real HIL requires the evidence described in
`docs/hil/atb-thermo-hardware-evidence-contract.md`.
