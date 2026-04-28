# I2C zero-heap HIL suite

This document is the hardware-in-the-loop acceptance contract for the ESP8266
zero-heap GPIO open-drain I2C master.

## Target

The dedicated target is:

```text
adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard_i2c_hil
```

It starts only the HIL firmware.  It does not start the normal event-driven demo
runtime.

## Commands

```sh
./tools/fw hil-i2c-build
./tools/fw hil-i2c-flash
./tools/fw hil-i2c-monitor
./tools/fw hil-i2c-run
```

`hil-i2c-monitor` exits with status 0 only after the serial log contains a HIL
summary with zero failures and the final success line.

## Executed tests

The suite executes these gates against the initialized `ev_i2c_port_t`:

| Gate | Default policy |
|---|---:|
| RTC register reads | 1000 reads from address `EV_BOARD_RTC_ADDR_7BIT`, register `0x00` |
| MCP23008 read/write | 1000 OLAT pattern write/read-back cycles while IODIR is held at all-inputs, then original IODIR/OLAT are restored |
| OLED partial flush | 16 bounded partial data flushes |
| OLED full scene flush | 1 full 72x40 scene flush |
| Missing device | address-only probe must return `EV_I2C_ERR_NACK` |
| SDA stuck-low | external fixture pulls SDA low; transaction must fail boundedly and recover after release |
| SCL held-low / stretch timeout | external fixture pulls SCL low; transaction must return bounded timeout and recover after release |
| GPIO IRQ flood during I2C | external fixture toggles an IRQ input while I2C runs; IRQ drops must not increase |

Every non-skipped test is wrapped by a heap gate: the free heap after the test
must not be lower than before the test.  This validates that the I2C transaction
path does not reintroduce runtime heap allocation.

## Fault-injection fixture

Fault GPIOs require explicit external open-drain-safe wiring.  The dedicated
ATNEL HIL target enables a lab profile through its `main/component.mk`; do not
reuse that target on a board without the fixture.  For other boards, override
the following board-profile macros only in a lab profile or build where the
fixture is physically installed:

```c
#define EV_BOARD_HIL_I2C_SDA_FAULT_GPIO       <gpio wired to pull SDA low>
#define EV_BOARD_HIL_I2C_SCL_FAULT_GPIO       <gpio wired to pull SCL low>
#define EV_BOARD_HIL_IRQ_FLOOD_OUTPUT_GPIO    <gpio wired to IRQ input>
#define EV_BOARD_HIL_IRQ_FLOOD_LINE_ID        <logical irq line id>
```

The SDA/SCL fixture outputs must be open-drain-safe.  Direct push-pull shorts
against the I2C bus are forbidden.

## Pass criteria

A valid qualification run must show both zero failures and zero skipped gates:

```text
HIL summary passed=<N> failed=0 skipped=0
EV_HIL_RESULT PASS failures=0 skipped=0
i2c zero-heap HIL completed successfully
```

Any skipped fixture gate makes the final suite return `EV_ERR_STATE`.  This is
intentional: the HIL target is an acceptance binary, not a best-effort smoke
test.
