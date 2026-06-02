# Operator transcript evidence workflow

This workflow is for recovery from a mixed operator terminal transcript.  The preferred release workflow remains to capture clean files separately:

```sh
./tools/fw sdk-build 2>&1 | tee build.log
./tools/fw sdk-memory-report 2>&1 | tee size.log
./tools/fw sdk-flash 2>&1 | tee flash.log
./tools/fw wemos-smoke-monitor 2>&1 | tee serial.log
```

A mixed transcript is not direct PASS evidence.  It may only be used as staging input for `tools/release/split_operator_transcript.py`, which redacts the input, records SHA-256 hashes, stores source line ranges, and splits it into clean sublogs.

## Staging command

```sh
python3 tools/release/split_operator_transcript.py \
  --input operator-terminal-transcript.log \
  --target wemos_esp_wroom_02_18650 \
  --output-dir docs/release/operator_transcript_evidence/wemos_esp_wroom_02_18650/current \
  --run-parsers
```

Expected output:

```text
operator_transcript.raw.log
build.log
flash.log
serial.raw.log
serial.normalized.log, if Wemos late-attach normalization is used
manifest.json
excerpt.md
sha256sums.txt
```

## Evidence rules

`build.log` is SDK PASS evidence only if it contains canonical SDK markers such as `EV_SDK_BUILD_TARGET=<target>` and `EV_SDK_BUILD_STATUS=PASS`.  Build noise without canonical SDK markers is staged as `NEEDS_STRICT_IMPORT`, not PASS.

`flash.log` may be checked by `tools/release/parse_esptool_flash_log.py`; flash PASS requires an ESP8266EX chip marker, write progress, and hash verification.

`serial.raw.log` may be checked by `tools/hil/parse_wemos_smoke_log.py --allow-runtime-alive-fallback --normalize`.  Wemos smoke fallback is accepted only for a clean extracted serial segment, never for the entire mixed transcript.  Deep-sleep/wake evidence remains strict and must not use runtime-alive fallback.

Private repo secrets are allowed by owner policy, but values must be redacted from staged logs and reports.

## Operator monitor exit footer classification

If an operator stops a raw serial monitor with Ctrl+C, a terminal wrapper may append:

```text
^C
--- exit ---
[process exited with code 130 (0x00000082)]
```

This is `SIGINT` from the operator, not a firmware panic. The splitter records it as `CONTROLLED_MONITOR_STOP` in `manifest.json` and writes the terminal wrapper lines to `operator_footer.log`. The footer is not direct PASS evidence. Smoke PASS still comes only from Wemos markers or the explicit runtime-alive fallback parser.
