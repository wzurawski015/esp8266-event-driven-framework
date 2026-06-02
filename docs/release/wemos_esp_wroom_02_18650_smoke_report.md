# Wemos smoke/deep-sleep evidence report

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Mode | smoke |
| Reason | Wemos hardware or serial log is not available in this environment. |
| Parsed evidence | `docs/release/hil_evidence/wemos_smoke/current/parsed.json` |

No PASS is declared without real serial evidence.

## Late-attach evidence note

If the raw UART log begins after early boot markers, Wemos smoke evidence may be
imported with the explicit runtime-alive fallback mode. The resulting `parsed.json`
must contain `runtime_alive_fallback=true`, raw log SHA-256, and normalized log
SHA-256 when normalization is used. Deep-sleep/wake evidence remains strict.
