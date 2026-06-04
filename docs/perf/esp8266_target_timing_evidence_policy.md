# ESP8266 target timing evidence policy

This policy adds target-side ESP8266 timing evidence to complement host `perf-gate`. Host benchmarks protect portable code against regressions; target timing records what the event-driven runtime does on real Wemos/ESP8266 serial output.

Private/lab repository secrets remain in the allowlisted `board_secrets.local.h` source file. Timing evidence tools redact generated logs and reports; they must not rewrite or remove the private secret file.

## Evidence source

The parser accepts a Wemos one-shot bundle or an explicit serial log. Timing samples are derived from target log timestamps such as:

```text
I (7356) ev_wroom02: EV_WEMOS_SMOKE_TICK seq=48
I (7360) ev_wroom02: EV_WEMOS_SMOKE_SNAPSHOT seq=48 pending=0
```

Logs without `I (<ms>)` timestamp prefixes may be valid smoke evidence, but they are not P50/P95/P99/P999 timing evidence.

## Initial policy

The first target timing gate is evidence-first. It fails reset/failure markers, non-monotonic samples and hard max-gap violations. P99/P999 are report-only until a real hardware baseline is committed. Do not turn missing hardware evidence into PASS.

## Relationship to Wemos one-shot

The Wemos one-shot manifest may include a separate `target_timing` section. Target timing does not replace SDK build PASS, flash PASS, Wemos smoke PASS or strict deep-sleep/wake PASS.
