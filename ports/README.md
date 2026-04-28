# ports

Stable interfaces that represent boundary crossings:

- time,
- GPIO,
- I2C,
- networking,
- persistence,
- reboot,
- logging sinks.

Ports describe *what* is needed, not *how* it is implemented.

## Stage 2A1 first public platform contracts

Public platform contracts now live under `ports/include/ev/`:

- `port_clock.h`
- `port_log.h`
- `port_reset.h`
- `port_gpio.h`
- `port_uart.h`

These headers define boundary surfaces only. They do not yet include concrete
ESP8266 RTOS SDK adapters.


Stage 2A4 adds the first concrete ESP8266 RTOS SDK implementations under
`adapters/esp8266_rtos_sdk/components/ev_platform/` for:

- clock
- log
- reset
- uart
