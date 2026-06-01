# SDK imported build/map/stack evidence report

Status: infrastructure added.

`tools/release/import_sdk_evidence.py` imports real local build logs, memory summaries,
map summaries and stack usage summaries into `docs/release/sdk_evidence/<target>/`.
Missing logs remain `NOT_RUN` / `ENVIRONMENT_BLOCKED`; PASS is not fabricated.

## Canonical SDK evidence rule

PASS requires `EV_SDK_BUILD_TARGET=<target>`, `EV_SDK_BUILD_STATUS=PASS`, `EV_SDK_BUILD_RC=0`, non-zero APP_BIN for buildable/HIL/physical targets and non-zero real memory evidence. Memory-report PASS is not SDK build proof.
