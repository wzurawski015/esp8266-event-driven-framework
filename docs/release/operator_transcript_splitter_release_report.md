# Operator transcript splitter release report

## Problem

A single terminal transcript can mix `wifi-secrets-status`, SDK build output, esptool flash output, and Wemos UART smoke output.  Passing that entire file directly to release evidence importers is unsafe because flash reset lines and build noise can be misinterpreted as serial evidence, and build noise without canonical SDK markers is not SDK PASS proof.

## Resolution

`tools/release/split_operator_transcript.py` splits the mixed transcript into clean staging artifacts:

- `operator_transcript.raw.log`, redacted;
- `build.log`;
- `flash.log`;
- `serial.raw.log`;
- `serial.normalized.log`, if Wemos normalization is used;
- `manifest.json` with segment SHA-256 and source line ranges;
- `excerpt.md`;
- `sha256sums.txt`.

## Release policy

The mixed transcript itself is never PASS evidence.  SDK PASS requires canonical SDK markers.  Flash PASS requires esptool parser proof.  Wemos smoke PASS may use strict markers or late-attach runtime-alive fallback from the extracted serial segment only.  Deep-sleep/wake evidence remains strict.

## Impact

- Runtime semantics: unchanged.
- Hot path: unchanged.
- Private repo secrets: unchanged and still allowed by owner policy.
- Evidence-Based Engineering: improved by source range tracking and staged parser outputs.
- Event-Driven evidence workflow: improved because late-attached Wemos UART logs can be used without accepting a full mixed transcript.
