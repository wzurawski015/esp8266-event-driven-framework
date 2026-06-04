# Release report consistency policy

Release reports are summaries of committed evidence. They are not evidence by themselves.

## Rules

- Self-tests are not release evidence.
- Gate modes are read-only.
- A `PASS`, `PASS_FULL_BUILD_FLASH_SMOKE`, `PASS_SMOKE_ONLY`, or `PASS_BUILD_ONLY` report must be backed by a committed `manifest.json`.
- Wemos one-shot PASS evidence must live under `docs/release/wemos_one_shot_evidence/<target>/...`.
- `test-run`, `/tmp`, `tmp*`, `TemporaryDirectory`, and `build/selftest` are never valid PASS evidence locations.
- If hardware evidence is missing, the correct status is `ENVIRONMENT_BLOCKED`, `NOT_RUN`, or `FAIL`, never a synthetic PASS.
- Private lab secrets remain in the allowlisted source file by owner policy. Real secret values must not appear in logs, manifests, reports, parsed JSON, or patches.

## Operational model

Use report/update targets only when real evidence has already been captured. Use gate targets to validate existing evidence without mutating tracked release documents.
