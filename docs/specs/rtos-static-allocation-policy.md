# RTOS static allocation policy

Status: environment hardening, not a full vendor-SDK heap elimination claim.

## Scope

The `esp8266-event-driven` runtime keeps strict zero-heap behavior on the
application hot paths:

- ISR ingress,
- actor runtime,
- mailbox push/pop,
- event publish and generated route spans,
- system/domain pumps,
- I2C runtime transactions,
- IRQ ring handling,
- application event loop.

The ESP8266 RTOS SDK v3.4 still has vendor bootstrap code that may allocate
memory internally. This remains a formal MISRA / Power-of-Ten exception limited
to system initialization and SDK compatibility. It is not a permission to use
heap in runtime paths.

## FreeRTOS kernel hooks

`./tools/fw sdk-check` injects guarded FreeRTOS static allocation support idempotently into the vendor `FreeRTOSConfig.h`. This replaces the previous fragile standalone FreeRTOSConfig patch-file approach that could fail Docker builds with malformed patch errors. The injection keeps dynamic allocation enabled as a bootstrap compatibility bridge:

```text
configSUPPORT_STATIC_ALLOCATION = 1
configSUPPORT_DYNAMIC_ALLOCATION = 1
```

The ESP8266 platform component provides static kernel task memory hooks in:

```text
adapters/esp8266_rtos_sdk/components/ev_platform/ev_freertos_static_hooks.c
```

The hook file provides static `.bss` memory for:

- the FreeRTOS Idle task, via `vApplicationGetIdleTaskMemory`,
- the FreeRTOS Timer Service task, via `vApplicationGetTimerTaskMemory` when
  `configUSE_TIMERS == 1`.

## Non-goals

This policy does not claim that the whole vendor SDK is heap-free. It also does
not replace ESP8266 SDK semaphore creation with `...Static` APIs, because prior
verification showed that the SDK does not reliably expose those APIs in this
configuration. Boot-time adapter semaphore creation remains covered by the
existing MISRA exception comments in the ESP8266 I2C and IRQ adapters.

## Verification gates

Required checks before release:

```sh
./tools/fw sdk-check
make -j2 host-test
./tools/fw routegen
git diff --exit-code
grep -RIn "malloc\\|calloc\\|realloc\\|free\\|strdup" core ports app adapters tests
grep -RIn "portMAX_DELAY" core ports app adapters tests
grep -RIn "xSemaphoreCreateMutexStatic\\|xSemaphoreCreateBinaryStatic" adapters/esp8266_rtos_sdk/components/ev_platform
```

`./tools/fw sdk-check` verifies that the SDK `FreeRTOSConfig.h` contains exactly one effective definition for each required static-allocation macro after idempotent injection. A full ESP8266 SDK build is still required for
final toolchain validation. Physical HIL is not implied by this policy.
