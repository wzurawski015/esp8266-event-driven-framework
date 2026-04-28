# esp8266_generic_dev target

This is the golden-reference SDK-native target project used for Stage 2 bring-up.

Scope of this target:

- validate the pinned ESP8266 RTOS SDK image,
- validate `sdkconfig.defaults -> defconfig -> build`,
- validate target-local cleanup symmetry through `sdk-clean-target` and `sdk-distclean`,
- prove the shared framework-backed boot/diagnostic harness on a board-neutral target,
- stay intentionally board-neutral,
- create a stable landing zone for later BSP and adapter integration.

Canonical commands from the repository root:

```bash
PORT="$(./tools/fw sdk-port-resolve)"
./tools/fw sdk-defconfig
./tools/fw sdk-build
./tools/fw sdk-clean-target
./tools/fw sdk-distclean
./tools/fw sdk-build

FW_ESPPORT="$PORT" ./tools/fw sdk-flash
FW_ESPPORT="$PORT" ./tools/fw sdk-flash-manual
FW_ESPPORT="$PORT" ./tools/fw sdk-simple-monitor
```

Notes:

- generated `sdkconfig` is intentionally ignored by Git,
- build artifacts remain inside this project-local target directory,
- runtime logs from the shared boot/diagnostic harness are expected at `115200`,
- `sdk-simple-monitor` is the preferred runtime path when Docker/WSL2 interactive monitor behavior is unstable,
- this target is intentionally small but no longer bypasses the public platform ports,
- the wrapper can auto-resolve the serial node when exactly one `/dev/ttyUSB*` or `/dev/ttyACM*` device is visible.

## CI policy

- `esp8266_generic_dev` must stay buildable without hardware attached,
- it must remain the smallest trustworthy target-side proof of the public ESP8266 ports,
- target-local cleanup and rebuild symmetry is part of its acceptance contract,
- hardware-specific features belong in dedicated BSP profiles.
