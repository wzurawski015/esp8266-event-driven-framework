# ESP8266 RTOS SDK adapters v1

Stage 2A4 introduces the first concrete ESP8266 RTOS SDK implementations of the
public platform ports.

Current adapters live in:

```text
adapters/esp8266_rtos_sdk/components/ev_platform/
```

## Implemented ports

- `ev_clock_port_t`
- `ev_log_port_t`
- `ev_reset_port_t`
- `ev_uart_port_t`
- `ev_i2c_port_t` (zero-heap software master)
- `ev_irq_port_t`
- `ev_onewire_port_t`


## Zero-heap I2C adapter note

The ESP8266 I2C adapter intentionally avoids `i2c_cmd_link_create()` and
`i2c_cmd_link_delete()` in runtime transactions.  Those SDK command-link calls
allocate descriptors dynamically and violate the post-bootstrap zero-heap
policy.  The adapter therefore uses one bootstrap-time mutex plus bounded GPIO
open-drain bit-banging, a fixed byte budget, clock-stretch timeout, and a
bounded bus-recovery sequence before each transaction.

## Shared bring-up harness

The current ESP8266 targets also share one framework-backed bring-up helper from
the same component.

That helper is not a new public `ports/` contract.
It is a target-side composition utility used to keep:

- `esp8266_generic_dev`, and
- `atnel_air_esp_motherboard`

on one boot/diagnostic path.

This is intentional. It reduces drift between the golden-reference target and the
first board target while the adapter surface is still small.

## Intentional limits of v1

This stage focuses on boot and diagnostics only.

The adapters are intentionally narrow:

- the clock adapter exposes monotonic time using SDK log timestamp granularity,
- wall-clock time is still reported as unsupported,
- the log adapter is task-context only and not ISR-safe,
- the UART adapter is tuned for UART0 boot/diagnostic flow,
- GPIO IRQ, I2C, and 1-Wire adapters are now present in the ESP8266 component; I2C is implemented as a bounded zero-heap GPIO open-drain software master instead of the SDK command-link API.

## Clock-resolution note

The public clock contract stays 64-bit and reports monotonic microseconds through
`ev_time_mono_us_t`.

On the current ESP8266 RTOS SDK v3.4 runtime path, the implementation derives
that value from `esp_log_early_timestamp()`, which is effectively millisecond
resolution.

So the contract unit remains microseconds, but the effective resolution is
currently **1 ms**.
The current source is also backed by a 32-bit millisecond counter, so the raw
source wraps after roughly 49.7 days of uptime.
This is acceptable for Stage 2 boot/diagnostic heartbeat work and is documented
explicitly so later BSP work can choose whether to keep or replace that source.

## Runtime log portability note

Target-side bring-up logs intentionally do **not** print the 64-bit microsecond
value directly with target-side 64-bit `printf` length modifiers.

On the current ESP8266 RTOS SDK v3.4 runtime path used for early diagnostics,
format strings that depend on platform-specific length modifiers are not reliable
enough for a clean serial stream.

For that reason, the current boot/diagnostic targets project monotonic time into
a diagnostic 32-bit millisecond value (`mono_now_ms`) for runtime logs only, and
print it with `%u` plus an explicit cast to `unsigned`.
This keeps the serial heartbeat readable without weakening the public platform
contract.

If long-uptime logging becomes a requirement later, the next step should be an
explicit high/low-part log format rather than reintroducing target-side 64-bit
`printf` dependence.

## Adapter hardening rules

The current adapter layer follows these invariants:

- USB modem-control lines used for bootloader entry are treated as operator workflow, not as part of `ev_uart_port_t`,
- unsupported UART ports fail explicitly with `EV_ERR_UNSUPPORTED`,
- invalid UART configuration fails explicitly with `EV_ERR_INVALID_ARG`,
- the UART adapter does not silently coerce unsupported line settings,
- log flushing is best-effort even before the UART driver is installed,
- target diagnostics may project rich public data into smaller runtime-only views when serial portability requires it,
- adapter-backed targets are expected to support `sdk-clean-target` and `sdk-distclean` through the Docker operator workflow,
- the generic and ATNEL targets should stay on the same shared bring-up helper until a stronger BSP split is justified.

## Why these four come first

These ports are enough to establish a framework-backed bring-up path on target
hardware:

1. identify the board,
2. identify the reset reason,
3. emit logs through the public logging contract,
4. keep a monotonic heartbeat through the public clock contract,
5. prove that UART console usage can be expressed through a public port.

## What follows next

Later Stage 2 steps should extend this with:

- GPIO
- I2C
- 1-Wire
- RTC / DS1337 integration
- MCP23008 integration
- IR and Magic Hercules helpers
