# Operator monitor exit classification report

## Status

`CONTROLLED_MONITOR_STOP` handling has been added for operator-interrupted serial monitor sessions.

## Problem

Some terminal wrappers print this footer when an operator ends a serial monitor with Ctrl+C:

```text
^C
--- exit ---
[process exited with code 130 (0x00000082)]
```

Exit code `130` means `128 + SIGINT`; it is an operator interrupt, not an ESP8266 firmware panic by itself.

## Contract

| Condition | Classification |
|---|---|
| `^C` or exit code `130` after valid serial evidence | `CONTROLLED_MONITOR_STOP` |
| Exit code `0` | `MONITOR_EXIT_OK` |
| Nonzero exit without SIGINT | `MONITOR_EXIT_NONZERO` |
| `panic`, exception, WDT, reset, or explicit `EV_*_FAIL` | Firmware/evidence failure wins over operator stop |

`CONTROLLED_MONITOR_STOP` is metadata only. It is never PASS proof by itself. Wemos smoke PASS still requires marker-based evidence or runtime-alive fallback. Wemos deep-sleep/wake remains strict and cannot use Ctrl+C footer evidence.

## Implementation

- `tools/release/operator_exit_footer.py` classifies terminal footers.
- `tools/release/split_operator_transcript.py` writes `operator_footer.log` separately from `serial.raw.log`.
- `tools/hil/parse_wemos_smoke_log.py` records operator footer fields in `parsed.json` while ignoring terminal wrapper lines as firmware evidence.
- `tools/serial_monitor.py` emits `EV_MONITOR_STOP reason=operator_sigint signal=SIGINT exit_code=0` when its own SIGINT handler exits cleanly.
- Static and release evidence contracts prevent treating code 130 as PASS without real smoke evidence.

## Evidence fields

```json
{
  "operator_interrupt_seen": true,
  "operator_exit_code": 130,
  "operator_exit_signal": "SIGINT",
  "operator_exit_classification": "CONTROLLED_MONITOR_STOP",
  "operator_footer_path": "operator_footer.log"
}
```

## Secrets policy

Private-repo secrets remain accepted by owner policy. Evidence tools still redact real secret values from logs and artefacts.
