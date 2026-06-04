# ESP8266 target timing integration report

This change adds target-side P50/P95/P99/P999 evidence capture from ESP8266 serial logs.

## Changes

- Added `tools/perf/parse_esp8266_target_timing.py`.
- Added `config/target_timing_budgets.json`.
- Added Makefile targets for timing self-test, report and gate.
- Wemos one-shot evidence can attach a `target_timing` manifest section.
- Static and release evidence contracts verify that target timing PASS is backed by source serial SHA-256 and real samples.

## Non-goals

Runtime semantics, QoS algorithms, mailbox layout, active route spans, hot-path zero-allocation and host performance budgets are unchanged.
