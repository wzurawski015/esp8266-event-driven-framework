# Wemos one-shot evidence run

| Field | Value |
|---|---|
| Status | FAIL |
| Reason | one or more workflow stages failed |
| Target | wemos_esp_wroom_02_18650 |
| Run ID | real-wemos-20260604T064940Z |
| Evidence dir | `docs/release/wemos_one_shot_evidence/wemos_esp_wroom_02_18650/current/runs/real-wemos-20260604T064940Z` |

| Stage | Status | Log | SHA-256 | Reason |
|---|---:|---|---|---|
| PREFLIGHT | PASS | `preflight.log` | `7b26414342c7e0bd438828aef5ba19b45b707b1943b97c9f741918147cc637eb` | preflight complete |
| SECRETS_STATUS | PASS | `secrets-status.log` | `07f38d0e94b1a73e2d1703e2fe1de8b49931e09c7a58d83194b5bb14a5689b40` | command completed |
| DISTCLEAN | PASS | `distclean.log` | `cd97e46d7332b5b564bef5e2b84719bba54d7562b3a8405b4a86e5661d17dec5` | command completed |
| DEFCONFIG | PASS | `defconfig.log` | `8f8acef5f6987638fdb448f955c16dfba058bb5e68f4115f584e7bc6f7041287` | command completed |
| BUILD | PASS | `build.log` | `0aeee56adea612e395fbddf503522856c48044c182a956d72e8c45cd2481ca69` | command completed |
| SIZE_MAP_STACK | PASS | `size.log` | `357fca123c1c72cf1119540c2ce3aa21b5c5d3146ac3a1605c8612269ff44d69` | command completed |
| FLASH | PASS | `flash.log` | `0c7ca3e6b1f2e8f15a9e3dbab6c101aba09abc038ed809e808b8e3b05576d857` | command completed |
| SERIAL_MONITOR | PASS | `serial.raw.log` | `13eec975f84c4d1d0ec442df2fe3e70b0c5d1999a1883fb28fe0d7523294e9b3` | command completed |
| PARSE_FLASH | PASS | `flash_evidence.json` | `1449e7d99e55e71dad5d5a7e48522fa6a570de6f914c0a0bec3c19dd704acde7` | parse_esptool_flash_log.py |
| PARSE_SMOKE | FAIL | `parsed.json` | `4a1ad1902b6f534707380e9060f36aeb26b4e816fb60ec6e34de03c9608295a0` | parse_wemos_smoke_log.py |
| PARSE_TARGET_TIMING | ENVIRONMENT_BLOCKED | `target_timing.json` | `71cbb72949d0c8f497ea45cb6477a84b5c7b593be8bc19de6256fa66040a7a0b` | parse_esp8266_target_timing.py |

This bundle keeps clean logs from the start: build, flash and serial evidence are separate files. Private repo secrets remain in the allowlisted source file and must not appear in evidence artifacts.
