# ESP8266 SDK memory budget report

The network-enabled ESP8266 firmware must be built with a reproducible memory
report before MQTT payload pools, telemetry, or remote commands are added. The
report is a build-time gate only; it does not claim WiFi, MQTT, flash, or HIL
success.

## Commands

Use the selected SDK target through `FW_SDK_PROJECT_DIR`:

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard
./tools/fw sdk-network-build-gate
```

The default gate is WiFi-only. It passes `EV_ESP8266_NET_ENABLE_MQTT=0` unless
MQTT is explicitly requested by the caller.

Existing build artifacts can be inspected without rebuilding:

```sh
./tools/fw sdk-memory-report
```

A build followed by a report can be requested with:

```sh
./tools/fw sdk-build-report
```

The repository-level matrix remains report-only by default:

```sh
make sdk-memory-matrix
```

Strict release mode is explicit and is intended for release jobs that have real
SDK build logs containing `EV_MEM_*` markers:

```sh
EV_SDK_MEMORY_REQUIRE_PASS=1 make sdk-memory-matrix
make sdk-memory-release-gate
```

On a host-only checkout without SDK build logs, `make sdk-memory-matrix` should
record `NOT_RUN` rows. That is an honest status, not a parser failure. Strict
mode may fail in that same checkout because there is no build evidence to prove
release memory budgets.

## Thresholds

The report supports optional environment thresholds:

```sh
EV_SDK_IRAM_LIMIT_BYTES=... \
EV_SDK_DRAM_LIMIT_BYTES=... \
EV_SDK_MAX_BSS_BYTES=... \
EV_SDK_MAX_DATA_BYTES=... \
EV_SDK_MAX_APP_BIN_BYTES=... \
EV_SDK_MAX_STACK_FRAME_BYTES=... \
./tools/fw sdk-network-build-gate
```

If a threshold is unset, the corresponding category is reported as unchecked or
unknown rather than guessed. The tool must not invent free IRAM/DRAM numbers,
application binary sizes, or stack facts when the build artifacts cannot prove
them.

## Output markers

The report emits stable machine-readable lines:

```text
EV_MEM_REPORT_START target=<project> elf=<path>
EV_MEM_SECTION name=<section> size=<bytes>
EV_MEM_IRAM used=<bytes> limit=<bytes|unchecked> free=<bytes|unknown> status=<ok|fail|unchecked>
EV_MEM_DRAM used=<bytes> limit=<bytes|unchecked> free=<bytes|unknown> status=<ok|fail|unchecked>
EV_MEM_BSS size=<bytes> limit=<bytes|unchecked> status=<ok|fail|unchecked>
EV_MEM_DATA size=<bytes> limit=<bytes|unchecked> status=<ok|fail|unchecked>
EV_MEM_APP_BIN size=<bytes|0> limit=<bytes|unchecked> status=<ok|fail|unchecked|not_available> source=<path|not_found>
EV_MEM_STACK_USAGE status=<ok|fail|unchecked|not_available> files=<n> entries=<n> max_frame=<bytes|0> limit=<bytes|unchecked> function=<name|unknown> qualifier=<kind|unknown> source=<path|not_found>
EV_MEM_REPORT_RESULT PASS failures=0 warnings=<N>
```

`EV_MEM_APP_BIN` is derived only from the application `.bin` that matches the
application ELF basename. If that relation cannot be proven, the marker reports
`not_available`; the matrix must not convert that state into PASS.

`EV_MEM_STACK_USAGE` is a per-function GCC `.su` max-frame baseline. It reports
how many `.su` files and entries were parsed, plus the function with the largest
single frame. It is deliberately **not** a call-chain worst-case stack proof.

The legacy `EV_MEM_STACK` marker is kept only as a compatibility breadcrumb and
points consumers to `EV_MEM_STACK_USAGE`.

## Secret handling

The memory report never prints compiler command lines or build flags. Local
network credentials from `board_secrets.local.h` or compile-time overrides must
not appear in the report.

## Static MQTT payload pool impact

The static MQTT payload foundation adds a bounded `.bss` cost when compiled:

```text
EV_NET_PAYLOAD_SLOT_COUNT * sizeof(ev_net_mqtt_rx_payload_t)
```

With default values this is four slots of 64-byte topic storage plus 128-byte
payload storage, plus small metadata. The SDK memory report gate should be run
after enabling MQTT to record the actual ELF section impact. Do not raise memory
thresholds without inspecting `EV_MEM_*` output.

## SDK linker-map release matrix

The release SDK memory gate uses `config/sdk_memory_budgets.def` and
`tools/sdk_memory_matrix.py`. Host `make memory-budget` remains a host static-size
gate; it is not a substitute for ESP8266 ELF/linker-map validation.

The matrix checks host-independent release evidence:

- ESP8266 ELF section budgets: IRAM, DRAM, BSS and DATA.
- Application binary size budget: `max_app_bin_bytes`.
- Stack `.su` baseline availability and max frame as report-only evidence.

The matrix default is report-only so host CI does not pretend that SDK artifacts
were built. Strict mode is opt-in and fails when non-metadata targets have no
PASS evidence.
