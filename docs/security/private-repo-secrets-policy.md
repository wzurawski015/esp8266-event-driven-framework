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
