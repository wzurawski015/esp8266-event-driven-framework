# ADR-0005: ESP8266 build, toolchain, and platform-contract strategy

## Status

Accepted.

## Context

Stage 1 established a platform-agnostic event kernel, delivery semantics,
lease-aware ownership, diagnostics, and cooperative pump layers. The next step
is to introduce ESP8266-facing infrastructure without contaminating `core/` and
`domain/` with ESP8266 RTOS SDK details.

ESP8266 RTOS SDK is documented around the GNU Make workflow (`make menuconfig`,
`make`, `make flash`, `make monitor`) and the official Linux setup documents the
`xtensa-lx106-elf-gcc8_4_0-esp-2020r3` toolchain. The project therefore needs a
single reproducible target-side toolchain contract before any adapter code is
written.

At the same time, the repository already has a stable Docker-first host/docs
workflow. We do not want to destabilize that path while target integration is
still being frozen.

Stage 2A1 also exposed a real upstream defect: ESP8266 RTOS SDK `v3.4` pulls
`components/coap/libcoap`, which in turn references a retired Eclipse-hosted
`tinydtls` URL. That breaks fully-recursive submodule checkout unless the URL is
rewritten to the active GitHub mirror.

## Decision

1. Stage 2A introduces a pinned ESP8266 SDK Docker image.
2. The canonical target-side baseline is:
   - ESP8266 RTOS SDK `v3.4`
   - `xtensa-lx106-elf-gcc8_4_0-esp-2020r3`
   - Docker-only access via `./tools/fw`
3. The target-side **release path remains the SDK-native GNU Make workflow**
   until real hardware bring-up is stable.
4. CMake may exist in the SDK image for experiments and evaluation, but it is
   not yet the release-defining firmware path.
5. Public platform contracts are introduced under `ports/include/ev/` before any
   concrete ESP8266 adapters are implemented.
6. `core/` and `domain/` must not include ESP8266 RTOS SDK headers.
7. The SDK image is allowed to carry a tightly-scoped URL rewrite for the dead
   `tinydtls` submodule origin as long as the workaround is documented and
   limited to that exact legacy URL.

## Consequences

### Positive

- Reproducible, auditable target-side toolchain contract.
- Alignment with the documented upstream ESP8266 RTOS SDK workflow.
- Clear platform boundary before bring-up code lands.
- No premature rewrite of the already-stable host/docs workflow.
- Builds remain robust despite the known upstream `tinydtls` submodule defect.

### Negative

- Host/docs and target firmware temporarily use different orchestration layers.
- CMake remains available but deliberately secondary.
- The first SDK image freezes a baseline that may later be tightened from tag to
  audited commit.
- The SDK image now carries one explicit upstream-compatibility workaround that
  should be removed if Espressif fixes the submodule URL upstream.

## Follow-up

- Stage 2A1: SDK image, wrapper commands, and platform-contract headers.
- Stage 2A2: minimal target firmware skeleton and first build lane.
- Stage 2B: adapter implementations for clock/log/reset/gpio/uart.
- Stage 2C: first BSP profile and minimal bring-up on real ESP8266 hardware.
