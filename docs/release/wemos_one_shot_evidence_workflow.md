# Wemos one-shot evidence workflow

The Wemos one-shot workflow records clean evidence from the start instead of relying on a mixed terminal transcript.

The private/lab repository intentionally keeps `bsp/wemos_esp_wroom_02_18650/board_secrets.local.h`. The file is not removed or rewritten by this workflow. Real secret values must not appear in logs, reports, manifests, parsed JSON, excerpts or patch artifacts.

## Modes

Preflight without hardware actions:

```sh
make wemos-one-shot-evidence-preflight
```

Capture with explicit operator intent:

```sh
export EV_WEMOS_ONE_SHOT_TARGET=wemos_esp_wroom_02_18650
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650
export EV_WEMOS_FLASH_VARIANT=4mb
export FW_MONITOR_BAUD=115200
export EV_WEMOS_ONE_SHOT_FLASH=1
export EV_HIL_ALLOW_FLASH=1
export EV_WEMOS_ONE_SHOT_MONITOR=1
export EV_HIL_ALLOW_MONITOR=1
export EV_WEMOS_ONE_SHOT_DISTCLEAN=1
make wemos-one-shot-evidence-capture
```

The workflow writes separate evidence files: `build.log`, `size.log`, `map_summary.txt`, `stack_usage.txt`, `flash.log`, `serial.raw.log`, optional `serial.normalized.log`, `manifest.json`, `operator_intent.json`, `excerpt.md` and `sha256sums.txt`.

## Status taxonomy

`PASS_FULL_BUILD_FLASH_SMOKE` means SDK build, flash and Wemos smoke evidence are all real PASS. `PASS_SMOKE_ONLY` is a limited operator-requested smoke-only result. `PARTIAL_EVIDENCE`, `ENVIRONMENT_BLOCKED`, `FAIL` and `NOT_RUN` are preserved and never softened into PASS.

`CONTROLLED_MONITOR_STOP` / terminal `code 130` is an operator Ctrl+C. It is not firmware panic and not proof of PASS by itself.
