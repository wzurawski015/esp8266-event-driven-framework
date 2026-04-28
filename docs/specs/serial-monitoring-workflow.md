# Serial monitoring workflow

This document freezes the current Docker-first serial monitoring policy for ESP8266 bring-up.

## Two monitor paths exist on purpose

`./tools/fw sdk-monitor` remains the SDK-native path.
It is still useful when the SDK monitor behavior itself is under investigation.

`./tools/fw sdk-simple-monitor` is the hardened fallback for day-to-day runtime diagnostics on real hardware.
It avoids the SDK monitor stack, runs without a pseudo-TTY, and opens the serial device directly inside the SDK container through a small Python serial reader with explicit signal handling. The reader is now launched directly as the container command, without an extra `sh -lc` wrapper layer between Docker and Python.

The simple monitor also disables software and hardware flow-control assumptions and requests exclusive access to the serial device when the installed `pyserial` supports it.

## Canonical Docker-first flow

From the repository root:

```bash
PORT="$(./tools/fw sdk-port-resolve)"

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard ./tools/fw sdk-build

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard \
FW_ESPPORT="$PORT" \
./tools/fw sdk-flash

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard \
FW_ESPPORT="$PORT" \
./tools/fw sdk-simple-monitor
```

If exactly one `/dev/ttyUSB*` or `/dev/ttyACM*` node is visible, `sdk-flash`, `sdk-flash-manual`, `sdk-monitor`, and `sdk-simple-monitor` can resolve it automatically.
Use `./tools/fw sdk-ports` to inspect visible candidates and `./tools/fw sdk-port-resolve` when you want the wrapper to print the resolved device path explicitly.

## When to use each command

Use `sdk-monitor` when:

- you want the SDK monitor UX,
- you need symbol-aware crash decoding later,
- pseudo-TTY handling is known to work in the current environment.

Use `sdk-simple-monitor` when:

- Docker + WSL2 interactive monitor behavior is unstable,
- the SDK monitor exits with TTY-related errors,
- you only need a clean runtime serial stream.

## Baud-rate policy

`tools/fw` now chooses the monitor baud from target context by default.
For the current boot/diagnostic ESP8266 targets, that default resolves to `115200`.

Boot ROM output from ESP8266 may still appear at a different baud rate during the earliest boot window.
That noise is acceptable as long as the post-boot runtime log stream is stable and readable.

## Runtime log formatting policy

For the current ESP8266 bring-up stage, runtime heartbeat logs print `mono_now_ms`
as a diagnostic 32-bit millisecond value.

This is intentional.
The public clock contract remains 64-bit in microseconds, but the serial diagnostics path avoids target-side `printf` length modifiers that are not stable on the current ESP8266 runtime path.

A runtime symptom of this portability problem is a line such as `mono_now_ms=lu`
instead of a numeric value.
When that appears, the target should be treated as a firmware formatting bug, not as a serial-line baud mismatch.

## Flash-reset fallback

`./tools/fw sdk-flash` still uses the SDK-native auto-reset path by default.
On Docker + WSL2 this may fail intermittently when USB modem-control ioctls for
DTR/RTS return an I/O error before `esptool.py` reaches the ROM loader.

The wrapper now retries only for transient connection/modem-control failures such as:

- DTR/RTS ioctl errors,
- `Errno 5` I/O failures,
- ROM-loader handshake timeouts / failed-connect paths,
- `Invalid head of packet (...)` during bootloader sync.

`Invalid head of packet` means the serial line responded, but the target was not yet in a clean ROM-loader sync state. On Docker + WSL2 this is usually the same operator class as a partial auto-reset / partial bootloader-entry failure, not a sign that the built image is corrupt.

`sdk-flash` now mirrors live `make flash` / `esptool.py` output while also classifying retryable failures. The wrapper forces `PYTHONUNBUFFERED=1` for the flash attempt, so a stall at `Connecting....` now reflects a real handshake wait instead of Python output being buffered into the temporary log stream.

Non-transient flash failures are returned immediately with a non-zero exit code.

When auto-reset retries do not recover, the canonical Docker fallback is:

```bash
PORT="$(./tools/fw sdk-port-resolve)"

FW_SDK_PROJECT_DIR=adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard \
FW_ESPPORT="$PORT" \
./tools/fw sdk-flash-manual
```

`sdk-flash-manual` now calls `esptool.py` directly with `--before no_reset --after no_reset` and assumes the board is already in ROM bootloader mode.
Use your board-specific equivalent of “hold BOOT/GPIO0 low, pulse RESET, then release into the loader” before starting that command.
If the command times out waiting for a packet header, treat that as an operator-state failure: the board never entered the ROM loader cleanly.
After a successful manual flash, press **RESET** to boot the new application image.

## Boot-capture rule

`./tools/fw sdk-simple-monitor` attaches to the live serial stream.
If you want to capture the application log from the first runtime line, start the monitor first and then press **RESET** on the board.

This keeps Docker as the canonical operator path without depending on the SDK interactive monitor implementation.

## Clock wrap-around note

The current monotonic implementation is derived from a 32-bit millisecond source.
That means the underlying source wraps after roughly 49.7 days of uptime.

For the current Stage 2 boot/diagnostic targets this is acceptable, because the
monotonic value is used as a short-horizon heartbeat and operator diagnostic.
The limitation is still documented explicitly so later BSP work can replace the
source if longer uninterrupted uptime diagnostics become a requirement.

## WSL2 note

On WSL2, serial access still depends on `usbipd` attach flow from Windows into WSL.
After attach, validate visible nodes with `./tools/fw sdk-ports` before invoking `./tools/fw`.

When the SDK-native monitor path fails under pseudo-TTY handling, prefer `sdk-simple-monitor` instead of bypassing Docker.
The simple monitor intentionally streams raw serial bytes and does not rely on `make simple_monitor`.

## Definition of done for target-side serial bring-up

The serial workflow is considered healthy when:

- `sdk-build` succeeds,
- `sdk-flash` succeeds,
- `sdk-simple-monitor` produces a stable runtime log stream,
- post-boot heartbeat logs are readable and monotonic,
- Docker remains the canonical operator path.
