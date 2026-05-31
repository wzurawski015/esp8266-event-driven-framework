# SDK imported build/map/stack evidence report

Status: infrastructure added.

`tools/release/import_sdk_evidence.py` imports real local build logs, memory summaries,
map summaries and stack usage summaries into `docs/release/sdk_evidence/<target>/`.
Missing logs remain `NOT_RUN` / `ENVIRONMENT_BLOCKED`; PASS is not fabricated.
