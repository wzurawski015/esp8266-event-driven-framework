# SDK evidence import workflow

Use this workflow after a real local ESP8266 SDK build. It imports only redacted,
textual evidence and never commits `.elf`, `.bin`, `.o`, `.a`, or full build directories.

Examples:

```sh
EV_SDK_EVIDENCE_IMPORT_ROOT=/path/to/sdk-evidence make sdk-import-evidence
make sdk-import-evidence-gate
make sdk-full-evidence-gate
```

Per-target import:

```sh
python3 tools/release/import_sdk_evidence.py \
  --import-target esp8266_generic_dev \
  --build-log /path/build.log \
  --map /path/map_summary.txt \
  --size-log /path/size.log \
  --stack /path/stack_usage.txt
```

Required PASS evidence includes `EV_SDK_BUILD_STATUS=PASS` or `EV_MEM_REPORT_RESULT PASS`
and non-zero `EV_MEM_*` values.
