# Wemos one-shot evidence run

| Field | Value |
|---|---|
| Status | ENVIRONMENT_BLOCKED |
| Reason | no committed Wemos one-shot manifest at `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current/manifest.json` |
| Target | wemos_esp_wroom_02_18650 |
| Evidence dir | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current` |

The Wemos one-shot tooling is present, but this release report is intentionally blocked until a real manifest-backed evidence bundle is committed under the documented evidence directory.

Self-test runs, `test-run` directories, temp paths and `build/selftest` artifacts are not release evidence. A PASS report requires a committed `manifest.json`, matching status, `sha256sums.txt`, and the logs required by the declared status.

Private repo secrets remain intentionally in the allowlisted source file. Real secret values must not appear in evidence artifacts.
