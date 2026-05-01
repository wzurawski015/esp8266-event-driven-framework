# Wemos ESP-WROOM-02 18650 smoke fixture

Target: `wemos_esp_wroom_02_18650`.

Operator notes:

- USB-UART bridge: CP2102 / CP210x, usually VID/PID `10c4:ea60`.
- Monitor baud: 115200 for runtime logs; earliest ROM boot text may appear at 74880.
- Manual bootloader entry if auto-reset is unreliable: hold FLASH, press/release RESET, release FLASH, then flash.
- GPIO16/D0 is the user LED and deep-sleep wake pin when tied to RST.

PASS requires both serial markers:

```text
EV_WEMOS_SMOKE_BOOT
EV_WEMOS_SMOKE_RUNTIME_READY
```

## Smoke monitor acceptance modes

The preferred PASS mode observes both firmware smoke markers:

```text
EV_WEMOS_SMOKE_BOOT
EV_WEMOS_SMOKE_RUNTIME_READY
```

If the monitor attaches after reset and misses those early markers, it may use a
runtime-alive fallback. That fallback requires at least three increasing
`ev_wroom02: diag actor: tick=` values and at least three increasing
`ev_wroom02: app actor: snapshot seq=` values, with no failure, exception or
reset-loop markers. The report records `mode=runtime_alive_fallback` when this
fallback is used.
