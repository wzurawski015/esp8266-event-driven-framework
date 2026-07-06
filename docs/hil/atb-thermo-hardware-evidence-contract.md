# ATB THERMO hardware evidence contract

This contract defines the evidence required before the
`atb_thermo_wemos_esp_wroom_02_4mb` target may be reported as real hardware PASS.
Parser self-tests and target boot markers are not HIL PASS.

Required physical setup:

- ATB THERMO Rev7 with Wemos ESP-WROOM-02 4 MB.
- JP2 in 2-3 normal mode so GPIO0/D3 is PWR_CTRL through Q2.
- Logic analyzer attached to I2C SCL/SDA and 1-Wire DQ when claiming bus timing PASS.
- WiFi enabled during 1-Wire timing evidence.

Required serial markers:

```text
EV_ATB_THERMO_BOOT target=atb_thermo_wemos_esp_wroom_02_4mb
EV_ATB_THERMO_PIN_MAP scl=5 sda=4 onewire=13 pwr=0 led=2 pir_check=14 audio=12 pir_dis=15
EV_ATB_THERMO_JP2_REQUIRED position=2-3 mode=normal_pwr_ctrl
EV_ATB_THERMO_PWR_CTRL gpio=0 active_low=1 state=ON result=OK
EV_ATB_THERMO_I2C_READY port=0 scl=5 sda=4 speed_hz=100000
EV_ATB_THERMO_ONEWIRE_READY dq=13
EV_DS18B20_TEMP cC=<signed-int> C=<fixed-point>
EV_BH1750_LIGHT mLux=<uint32> lux=<fixed-point>
```

Required I2C analyzer evidence at 100 kHz:

- START.
- address ACK.
- address NACK plus STOP and bus idle.
- repeated START.
- read byte stream.
- final read-byte NACK.
- STOP release with SDA/SCL high.
- SDA stuck-low recovery and post-recovery probe.

Required 1-Wire evidence with WiFi ON:

- reset low/high timing.
- presence pulse.
- read/write slot timing.
- scratchpad CRC PASS.
- DQ release after operation.

Missing evidence must be reported as `ENVIRONMENT_BLOCKED`, not `PASS`.
