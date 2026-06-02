# Operator transcript splitter report

| Field | Value |
|---|---|
| Status | INFRASTRUCTURE_READY |
| Tool | `tools/release/split_operator_transcript.py` |
| Gate | `make operator-transcript-split-self-test` |
| Runtime impact | None |

The splitter handles mixed operator transcripts that contain SDK build output, esptool flashing output, and Wemos UART smoke output in one file.  The complete transcript remains staging input only and is never direct PASS evidence.

The splitter creates redacted sublogs with SHA-256 hashes and source line ranges.  It may run strict downstream parsers for flash and Wemos smoke evidence.  SDK build segments without canonical SDK markers remain `NEEDS_STRICT_IMPORT`.
