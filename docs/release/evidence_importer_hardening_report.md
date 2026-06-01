# Evidence importer hardening report

## Scope

This patch hardens SDK, flash and HIL evidence importers. It does not change the
runtime, actor model, mailbox layout, route table, QoS contract, deep-sleep state
machine, graph storage, hotpath contract or performance budgets.

## Before

SDK import could accept evidence when `EV_MEM_REPORT_RESULT PASS` and non-zero
memory-like values appeared in a log. That was too permissive because parser
self-tests and mixed terminal transcripts can contain such lines without proving
that a real SDK target was built.

HIL parsers also read user-provided log paths directly, so placeholder paths or
missing files could raise operator-facing Python tracebacks instead of controlled
release statuses.

## After

Real SDK PASS requires all of the following:

- `EV_SDK_BUILD_TARGET=<target>` exactly matching the imported target.
- `EV_SDK_BUILD_STATUS=PASS`.
- No self-test markers.
- No mixed transcript markers.
- Non-zero `APP_BIN` for buildable, physical-smoke and HIL targets.
- At least one non-zero real memory section marker.

`EV_MEM_REPORT_RESULT PASS` is not sufficient and is rejected when it comes from
self-test or mixed transcript output.

HIL and flash parsers now use controlled path checks. Missing files and literal
placeholder paths become `ENVIRONMENT_BLOCKED`; directories or unreadable inputs
become `FAIL` with an explicit reason. Normal operator mistakes do not produce a
Python traceback.

## New flash evidence

`tools/release/parse_esptool_flash_log.py` parses `esptool.py` logs separately
from SDK build evidence. PASS requires ESP8266EX chip detection, write progress,
`Hash of data verified.`, and a reset/leave marker. Flash evidence is stored as
`flash.log`, `flash_evidence.json`, `flash_excerpt.md`, and SHA-256 manifest.

## Validation

The hardening gate runs SDK importer self-tests, flash parser self-tests, HIL
parser self-tests, eventflow self-tests, release evidence contracts and static
contracts.

## Impact

This patch may reject logs that were previously accepted. That is intentional:
release evidence must be more strict than human terminal transcripts. The result
improves Evidence-Based Engineering and Event-Driven release credibility by
removing fake-PASS paths.

## Canonical capture follow-up

Automatic SDK capture follows the same strict policy as manual import: target-specific build target marker, build status PASS, build return code zero, non-zero APP_BIN for required target classes and non-zero real memory evidence. Memory-report PASS alone is never SDK build proof.
