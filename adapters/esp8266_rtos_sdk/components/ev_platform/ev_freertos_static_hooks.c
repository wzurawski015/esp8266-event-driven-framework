#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*
 * MISRA EXCEPTION / RTOS ENVIRONMENT HARDENING:
 *
 * ESP8266_RTOS_SDK v3.4 does not provide reliable project-level Kconfig
 * control for a fully static FreeRTOS configuration. The project therefore
 * keeps the previously approved vendor-bootstrap exception for SDK-owned heap
 * use, while enabling the application to provide static kernel task memory
 * when the SDK FreeRTOSConfig.h exposes configSUPPORT_STATIC_ALLOCATION
 * through idempotent sdk-check injection or pre-injected SDK image configuration.
 *
 * These hooks are adapter-layer only. They provide static .bss storage for the
 * FreeRTOS Idle task and, when enabled by the SDK, the Timer Service task.
 * They do not permit runtime heap use in actor, mailbox, publish, route, ISR,
 * I2C, IRQ, or application hot paths.
 */

#if defined(configSUPPORT_STATIC_ALLOCATION) && (configSUPPORT_STATIC_ALLOCATION == 1)

#if defined(configIDLE_TASK_STACK_SIZE)
#define EV_FREERTOS_IDLE_TASK_STACK_WORDS (configIDLE_TASK_STACK_SIZE)
#elif defined(configMINIMAL_STACK_SIZE)
#define EV_FREERTOS_IDLE_TASK_STACK_WORDS (configMINIMAL_STACK_SIZE)
#else
#error "FreeRTOS static idle task hook requires configIDLE_TASK_STACK_SIZE or configMINIMAL_STACK_SIZE"
#endif

static StaticTask_t s_ev_freertos_idle_tcb;
static StackType_t s_ev_freertos_idle_stack[EV_FREERTOS_IDLE_TASK_STACK_WORDS];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    if (ppxIdleTaskTCBBuffer != NULL) {
        *ppxIdleTaskTCBBuffer = &s_ev_freertos_idle_tcb;
    }
    if (ppxIdleTaskStackBuffer != NULL) {
        *ppxIdleTaskStackBuffer = s_ev_freertos_idle_stack;
    }
    if (pulIdleTaskStackSize != NULL) {
        *pulIdleTaskStackSize = (uint32_t)EV_FREERTOS_IDLE_TASK_STACK_WORDS;
    }
}

#if defined(configUSE_TIMERS) && (configUSE_TIMERS == 1)

#if !defined(configTIMER_TASK_STACK_DEPTH)
#error "FreeRTOS static timer task hook requires configTIMER_TASK_STACK_DEPTH"
#endif

static StaticTask_t s_ev_freertos_timer_tcb;
static StackType_t s_ev_freertos_timer_stack[configTIMER_TASK_STACK_DEPTH];

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    if (ppxTimerTaskTCBBuffer != NULL) {
        *ppxTimerTaskTCBBuffer = &s_ev_freertos_timer_tcb;
    }
    if (ppxTimerTaskStackBuffer != NULL) {
        *ppxTimerTaskStackBuffer = s_ev_freertos_timer_stack;
    }
    if (pulTimerTaskStackSize != NULL) {
        *pulTimerTaskStackSize = (uint32_t)configTIMER_TASK_STACK_DEPTH;
    }
}

#endif /* defined(configUSE_TIMERS) && (configUSE_TIMERS == 1) */
#endif /* defined(configSUPPORT_STATIC_ALLOCATION) && (configSUPPORT_STATIC_ALLOCATION == 1) */
