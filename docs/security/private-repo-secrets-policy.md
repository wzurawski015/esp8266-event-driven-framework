# Private-repo secrets policy

This repository may operate in `PRIVATE_REPO_MODE` for local laboratory work. In that mode, the Wemos BSP-local `board_secrets.local.h` file is intentionally allowed to remain in the private repository so the active board profile is reproducible on the owner machine.

This policy does **not** turn secrets into public release material. The repository may keep the local BSP file, but tooling, reports and generated artifacts must treat every credential value as confidential.

## Accepted private mode

`PRIVATE_REPO_MODE` is the default mode for this project snapshot:

- `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h` is the allowlisted location for real WiFi credentials and command tokens in this snapshot.
- The Wemos lab profile may keep its local `board_secrets.local.h` tracked by the private repository owner.
- Tools must not print literal values of `EV_BOARD_NET_WIFI_SSID`, `EV_BOARD_NET_WIFI_PASSWORD` or `EV_BOARD_NET_COMMAND_TOKEN`.
- Reports, validation logs, generated Markdown, generated JSON/JSONL, generated headers and patch artifacts must not copy literal secret values.
- Error messages may mention file path, line number and macro name, but the value must be rendered as `<REDACTED>`.

## Public release mode

`PUBLIC_RELEASE_MODE` is enabled by running the checker with:

```sh
PUBLIC_RELEASE=1 python3 tools/audit/private_repo_secrets_policy.py
```

In that mode, any real `board_secrets.local.h` value is a hard failure. A public archive must use only placeholder examples such as `YOUR_WIFI_PASSWORD` or `<REDACTED>`.

## Required gates

Private containment gate:

```sh
python3 tools/audit/private_repo_secrets_policy.py
```

Public release safety gate:

```sh
make public-release-safety-gate
```

`make quality-gate` uses the private containment policy. Public release packaging must explicitly run `make public-release-safety-gate` and is expected to fail while real local secrets are intentionally tracked.

## Evidence redaction rule

Shared redaction tooling must redact by both pattern and value:

- pattern-based redaction covers key/value lines and ESP SDK WiFi status lines;
- value-based redaction loads current literal values only from allowlisted
  private-lab secret files and replaces copied values in memory with
  `<REDACTED>`.

This value lookup is permitted only for containment/redaction.  Tools must never
print or serialize the loaded values.

## Phase 2 privacy classes

Secret containment is now class-based rather than a broad path bypass.

| Class | Private repo | Public release | Notes |
|---|---:|---:|---|
| `PRIVATE_LAB_SECRET_SOURCE` | allowed | forbidden | Explicit BSP-local secret source files only. |
| `PRIVATE_LAB_RAW_EVIDENCE` | allowed | forbidden | Explicit raw/private lab transcripts such as `serial.raw.log`; never exported to public bundles. |
| `PUBLIC_SANITIZED` | secret values forbidden | secret values forbidden | Redacted, normalized, summary, report, JSON/Markdown and hash sidecars. |
| `UNKNOWN` | secret values forbidden | secret values forbidden | Ordinary source/documentation paths. |

The canonical classifier is `tools/lib/ev_privacy_classification.py`.  The
private policy still scans all text files and value-matches the current allowed
secrets, but skips only paths classified as private-lab source/raw evidence in
`PRIVATE_REPO` mode.  Files named `*.redacted.*`, `*.normalized.*`, reports,
JSON/JSONL and Markdown remain sanitized-only even in a private repo.

`make evidence-redaction-scrub` sanitizes committed public/redacted evidence and
refreshes Wemos one-shot SHA sidecars while leaving explicit raw/private evidence
untouched.  This is intentionally different from a global `docs/release/**`
allowlist: raw evidence can remain private; redacted evidence must be true.

## Phase 3 gate discipline

Quality and release gates are check-only. `make evidence-redaction-scrub` and
`make repair-evidence-redaction` are operator repair commands; they must be run
and committed before release gates, not hidden inside `quality-gate`.

CI verifies this by running a clean-tree check after `./tools/fw release-gate`.
If a generator, scrubber or doc target changes tracked files during a gate, the
build must fail so the sanitized artifacts can be reviewed and committed in a
separate private-repo change.

SDK warning policy is split deliberately:

- `sdk-project-warning-self-test` validates parser behavior in host-only jobs.
- `sdk-project-warning-gate` requires `EV_SDK_BUILD_LOG` and is used by SDK jobs
  with a fresh build log captured from the current build session.

This keeps private-lab secrets allowed in explicit private classes while keeping
`redacted`, `normalized`, reports, manifests and public artifacts truthful.
