# ATB THERMO Wemos ESP-WROOM-02 4 MB SDK target

This target binds the portable event-driven runtime to the ATB THERMO Rev7
carrier board. It must not be confused with the minimal
`wemos_esp_wroom_02_18650` smoke target.

## Hardware setup

Set JP2 to pins 2-3 for normal operation. GPIO0/D3 is then the boot-sensitive
PWR_CTRL line through Q2. Do not flash or boot with JP2 in the wrong position.

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

Expected bring-up markers include `EV_ATB_THERMO_PIN_MAP`,
`EV_ATB_THERMO_I2C_READY`, `EV_ATB_THERMO_ONEWIRE_READY`,
`EV_DS18B20_TEMP` and `EV_BH1750_LIGHT` when the sensors are present.

Real HIL PASS is not emitted by this application. HIL requires physical serial
logs and logic-analyzer captures following `docs/hil/atb-thermo-hardware-evidence-contract.md`.
