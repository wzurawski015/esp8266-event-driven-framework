# Eventflow one-shot Wemos integration report

The eventflow release gate can consume a Wemos one-shot evidence bundle as a formal source of release readiness.

Smoke PASS may be strict marker-based or a clearly marked `runtime_alive_fallback` / firmware runtime-alive source. Deep-sleep PASS remains strict: it requires ordered power-state markers and wake boot evidence. Runtime-alive fallback cannot promote deep-sleep evidence.

Use:

```sh
make eventflow-one-shot-evidence-gate EV_WEMOS_ONE_SHOT_EVIDENCE_DIR=docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/runs/<run-id>
```
