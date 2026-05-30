# Known limitations

- ESP8266 RTOS SDK firmware compilation was not executed in this environment because the Xtensa/ESP8266 SDK toolchain is not available here.
- Hardware-in-the-loop validation was not executed because no physical ESP8266 board or serial/HIL profile is attached to this environment.
- The original device actor implementations are preserved in the compatibility core layer while `modules/` and `drivers/` provide the new framework-facing split. A deeper mechanical move of all device actor source files can be done later with the same gates.
- `make safety-gate` is host-only and intentionally separate from `make quality-gate` in this patch. Environments without ASAN/UBSAN support must report `ENVIRONMENT_BLOCKED` instead of a false PASS.
- ESP8266 RTOS SDK builds are not forced to inherit host C17 `-Werror` or sanitizer flags because the Xtensa SDK toolchain can have different warning behavior.
