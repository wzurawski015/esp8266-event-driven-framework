# BME280 entry criteria after Phase 8

BME280 is intentionally deferred until the bus and release evidence foundation is
strong enough to make a more complex compensated sensor driver boring.

Entry criteria before adding a BME280 driver or actor:

1. Release archive is self-clean: `make release-prearchive-gate` passes and the
   archive is produced through `./tools/fw release-archive`.
2. Wemos one-shot SDK build evidence uses markerized `sdk-build-one` and fresh
   `sdk_warning_policy.py` checks.
3. Real I2C logic-analyzer HIL exists for STOP-after-NACK, final-NACK+STOP,
   stuck-low recovery and clock-stretch/timeout evidence, or non-HIL CI reports
   `ENVIRONMENT_BLOCKED` without false PASS.
4. Real OneWire WiFi-on HIL exists for DS18B20 timing/CRC/release evidence, or
   non-HIL CI reports `ENVIRONMENT_BLOCKED` without false PASS.
5. Runtime soak evidence passes with WiFi on, zero dropped events, zero deadline
   misses, zero hot-path heap allocations and no WDT reset.
6. SDK memory/stack regression evidence passes for supported SDK targets.
7. BH1750 actor/driver contract passes as the simple `read_stream` reference
   sensor.
8. All five quality pillars remain at or above 95% without synthetic evidence.

BME280 must follow the same Clean Architecture split: compensation and register
protocol in `drivers/`; actor code only schedules, retries, applies backoff and
publishes events.
