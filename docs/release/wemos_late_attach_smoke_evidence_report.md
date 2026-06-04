# Wemos late-attach smoke evidence report

| Field | Value |
|---|---|
| Status | INFRASTRUCTURE_READY |
| Scope | Wemos smoke evidence only |
| Runtime semantics changed | No |
| Deep-sleep evidence fallback | Not allowed |
| Private repo secrets | Accepted by owner policy; values must not be logged |

## Problem

A serial monitor can attach after the earliest `printf()` boot markers from
`app_main()`. In that case the raw UART log can still contain a healthy runtime
sequence such as `EV_WEMOS_SMOKE_TICK` and `EV_WEMOS_SMOKE_SNAPSHOT`, but miss
`EV_WEMOS_SMOKE_BOOT` or the earliest `EV_WEMOS_SMOKE_RUNTIME_READY` line.

## Firmware hardening

The Wemos board config enables target-local smoke markers in the ESP8266 runtime
adapter. After the runtime log port is initialized and the demo runtime has
published boot, the adapter emits a late-observable runtime-ready marker. After
three successful tick/snapshot samples it emits exactly one runtime-alive smoke
result:

```text
EV_WEMOS_SMOKE_RUNTIME_READY source=runtime_app
EV_WEMOS_SMOKE_TICK seq=<n>
EV_WEMOS_SMOKE_SNAPSHOT seq=<n>
EV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=firmware_runtime_alive
```

The result marker is guarded by `smoke_runtime_alive_result_after_samples`; it is
not emitted before the sample threshold and is not a deep-sleep proof.

## Parser hardening

`tools/hil/parse_wemos_smoke_log.py` now has two smoke modes:

- default strict mode, which requires boot, runtime-ready and result markers;
- `--allow-runtime-alive-fallback`, which can accept late-attached logs only
  when tick/snapshot sequences are monotonic, long enough and free of reset,
  panic, exception or watchdog markers.

Fallback evidence is written as:

```json
{
  "mode": "runtime_alive_fallback",
  "runtime_alive_fallback": true,
  "marker_based": false
}
```

When `--normalize` is used, both the raw and normalized logs are retained:

```text
serial.raw.log
serial.normalized.log
serial.log
parsed.json
excerpt.md
sha256sums.txt
```

## Deep-sleep remains strict

Runtime-alive fallback is never accepted for deep-sleep/wake evidence. Deep-sleep
PASS still requires the ordered power state markers, deep-sleep enter, wake boot,
wake reason and `EV_POWER_SMOKE_RESULT PASS`.

## Validation

Required validation:

```sh
python3 tools/hil/parse_wemos_smoke_log.py --self-test
python3 tools/hil/import_hil_serial_evidence.py --self-test
python3 tools/hil/eventflow_evidence_gate.py --self-test
python3 tools/audit/release_evidence_contracts.py
python3 tools/audit/static_contracts.py
make hil-wemos-smoke-late-attach-self-test
```

## Impact

This improves Wemos smoke release evidence and Filar 5 by making real runtime
logs robust to late serial monitor attachment without weakening deep-sleep or
hardware eventflow requirements.

## Operator interrupt footer

Late-attached Wemos smoke evidence may end with `^C` and `[process exited with code 130]` when the operator stops the monitor. The parser records this as `CONTROLLED_MONITOR_STOP` while preserving the requirement for monotonic tick/snapshot evidence. Deep-sleep/wake evidence remains strict.
