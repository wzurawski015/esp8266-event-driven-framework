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

## Hardening note: no mixed terminal transcript

Do not import a combined shell transcript as SDK evidence. A valid SDK build log
must contain target-specific `EV_SDK_BUILD_TARGET=<target>` and
`EV_SDK_BUILD_STATUS=PASS`. `EV_MEM_REPORT_RESULT PASS` from self-tests is never
sufficient to prove a real SDK build.

For buildable, physical-smoke and HIL SDK targets, `APP_BIN` must be non-zero and
at least one real memory section marker must be non-zero. Binary artifacts such
as `.elf`, `.bin`, `.o`, and `.a` remain rejected from committed evidence.
