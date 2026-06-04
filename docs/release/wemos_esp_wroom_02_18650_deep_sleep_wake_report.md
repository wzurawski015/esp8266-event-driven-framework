# Wemos smoke/deep-sleep evidence report

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Mode | deepsleep |
| Reason | Wemos hardware or serial log is not available in this environment. |
| Parsed evidence | `docs/release/hil_evidence/wemos_deepsleep/current/parsed.json` |

No PASS is declared without real serial evidence.

## One-shot strict source

`make wemos-one-shot-deepsleep-evidence-capture` records a clean serial log and parses it with strict `--deepsleep` mode. The runtime-alive fallback used for smoke-only evidence is not accepted for wake/deep-sleep proof.
