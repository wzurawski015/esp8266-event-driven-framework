# Known limitations

- ESP8266 RTOS SDK firmware compilation was not executed in this environment because the Xtensa/ESP8266 SDK toolchain is not available here.
- Hardware-in-the-loop validation was not executed because no physical ESP8266 board or serial/HIL profile is attached to this environment.
- The original device actor implementations are preserved in the compatibility core layer while `modules/` and `drivers/` provide the new framework-facing split. A deeper mechanical move of all device actor source files can be done later with the same gates.
- The host compiler gate uses strict warnings from the original project style. It does not force `-Werror` globally because the migrated legacy codebase was preserved and the acceptance requirement was to keep behavior while adding gates.
