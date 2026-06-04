# ESP8266 I2C SDK-bug avoidance boundary

This project intentionally does **not** use the ESP8266 RTOS SDK I2C command-link driver in the runtime I2C path.
The ESP8266 event-platform adapter uses a bounded GPIO open-drain software I2C master instead.

## Risk captured by the SDK patch

`tools/patches/0001-esp8266-i2c-stop-and-speed-fix.patch` documents the class of failures this boundary avoids:

1. misplaced or redundant `i2c_master_wait(...)` delays can disturb bit-level I2C timing;
2. an ACK/NACK error path can return without a complete STOP/release sequence, leaving SDA/SCL in a non-idle state.

A mission-grade I2C foundation must not depend on these command-link paths.

## Required bus completion rules

Every runtime transaction must be bounded and must end in one of two auditable states:

- success after START/address/payload/STOP and bus release;
- bounded failure with STOP/release where possible, or explicit bus recovery/fault if a line is stuck.

ACK/NACK errors are not allowed to leave the bus in an undefined state. SCL waits must be bounded by the clock-stretch timeout. SDA/SCL stuck-low cases must have a recovery/fault path.

## Zero-heap runtime rule

The runtime I2C transaction path must remain zero-heap. The ESP8266 adapter may use a boot-time `xSemaphoreCreateMutex()` bootstrap exception, but no transaction may allocate or free heap memory.

## Forbidden SDK command-link boundary

Portable layers and the ESP8266 I2C adapter must not use:

```text
driver/i2c.h
i2c_cmd_link_create
i2c_master_cmd_begin
i2c_driver_install
```

The tokens may appear only in documentation, audit tools, or the historical SDK patch that explains why the boundary exists.

## Sensor-readiness implication

Before adding BME280, BH1750, or further DS18B20 flows, the bus contract must be complete enough to express register transactions and raw stream reads, and HIL must prove ACK error STOP/release and stuck-line containment on real hardware.
