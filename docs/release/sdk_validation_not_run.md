# SDK validation not run

ESP8266 RTOS SDK target skeletons are included, but SDK firmware builds were not executed.

Reason:

- no ESP8266 RTOS SDK installation is available in this execution environment,
- no Xtensa ESP8266 toolchain is available,
- no physical HIL target is attached.

Validated instead:

- route generation,
- static contracts,
- memory budget probe,
- host tests,
- deterministic property tests.
