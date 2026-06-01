# HIL serial evidence import workflow

Import only real serial logs captured from ATNEL and Wemos hardware:

```sh
EV_HIL_ATNEL_I2C_SERIAL_LOG=/path/atnel-i2c.log \
EV_HIL_WEMOS_SMOKE_SERIAL_LOG=/path/wemos-smoke.log \
EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG=/path/wemos-deepsleep.log \
make hil-import-all-evidence

make hil-real-evidence-gate
```

PASS requires strict marker sequences and parsed JSON. Missing hardware/logs remain
`ENVIRONMENT_BLOCKED` or `NOT_RUN`.

## Hardening note: placeholder paths

Do not use placeholder paths such as `/path/atnel-i2c.log` or
`/path/wemos-smoke.log`. Importers now report these as `ENVIRONMENT_BLOCKED`
without traceback. HIL PASS requires parsed JSON generated from a real serial log
with the required markers.
