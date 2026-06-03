# Wemos one-shot evidence run

| Field | Value |
|---|---|
| Status | PASS_FULL_BUILD_FLASH_SMOKE |
| Reason | SDK build, flash and smoke evidence are PASS |
| Target | wemos_esp_wroom_02_18650 |
| Run ID | test-run |
| Evidence dir | `tmpqs_j4cg6/runs/test-run` |

| Stage | Status | Log | SHA-256 | Reason |
|---|---:|---|---|---|

This bundle keeps clean logs from the start: build, flash and serial evidence are separate files. Private repo secrets remain in the allowlisted source file and must not appear in evidence artifacts.

## Eventflow consumption

The bundle can be consumed by `eventflow-one-shot-evidence-gate`. The gate reads `manifest.json`, accepts Wemos smoke PASS according to its declared mode, and accepts deep-sleep PASS only with strict marker proof.
