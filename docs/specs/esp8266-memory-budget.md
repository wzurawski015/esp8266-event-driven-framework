# ESP8266 SDK memory budget report

The network-enabled ESP8266 firmware must be built with a reproducible memory
report before MQTT payload pools, telemetry, or remote commands are added.  The
report is a build-time gate only; it does not claim WiFi, MQTT, flash, or HIL
success.

## Commands

Use the selected SDK target through `FW_SDK_PROJECT_DIR`:

```sh
export FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard
./tools/fw sdk-network-build-gate
```

The default gate is WiFi-only.  It passes `EV_ESP8266_NET_ENABLE_MQTT=0` unless
MQTT is explicitly requested by the caller.

Existing build artifacts can be inspected without rebuilding:

```sh
./tools/fw sdk-memory-report
```

A build followed by a report can be requested with:

```sh
./tools/fw sdk-build-report
```

## Thresholds

The report supports optional environment thresholds:

```sh
EV_SDK_IRAM_LIMIT_BYTES=... \
EV_SDK_DRAM_LIMIT_BYTES=... \
EV_SDK_MAX_BSS_BYTES=... \
EV_SDK_MAX_DATA_BYTES=... \
./tools/fw sdk-network-build-gate
```

If a threshold is unset, the corresponding category is reported as unchecked or
unknown rather than guessed.  The tool must not invent free IRAM/DRAM numbers
when the ELF/map data cannot prove them.

## Output markers

The report emits stable machine-readable lines:

```text
EV_MEM_REPORT_START target=<project> elf=<path>
EV_MEM_SECTION name=<section> size=<bytes>
EV_MEM_IRAM used=<bytes> limit=<bytes|unchecked> free=<bytes|unknown> status=<ok|fail|unchecked>
EV_MEM_DRAM used=<bytes> limit=<bytes|unchecked> free=<bytes|unknown> status=<ok|fail|unchecked>
EV_MEM_BSS size=<bytes> limit=<bytes|unchecked> status=<ok|fail|unchecked>
EV_MEM_DATA size=<bytes> limit=<bytes|unchecked> status=<ok|fail|unchecked>
EV_MEM_STACK status=not_available source=elf_section_report
EV_MEM_REPORT_RESULT PASS failures=0 warnings=<N>
```

Stack usage is reported as unavailable unless a later tool can derive it from
map/ELF data without guessing.

## Secret handling

The memory report never prints compiler command lines or build flags.  Local
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
