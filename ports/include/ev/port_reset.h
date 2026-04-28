#ifndef EV_PORT_RESET_H
#define EV_PORT_RESET_H

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Normalized reset reasons exposed to the framework.
 */
typedef enum ev_reset_reason {
    EV_RESET_REASON_UNKNOWN = 0,
    EV_RESET_REASON_POWER_ON = 1,
    EV_RESET_REASON_SOFTWARE = 2,
    EV_RESET_REASON_WATCHDOG = 3,
    EV_RESET_REASON_DEEP_SLEEP = 4,
    EV_RESET_REASON_EXTERNAL = 5,
    EV_RESET_REASON_BROWNOUT = 6
} ev_reset_reason_t;

/**
 * @brief Platform reset contract.
 */
typedef struct ev_reset_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*get_reason)(void *ctx, ev_reset_reason_t *out_reason); /**< Query the last reset reason. */
    ev_result_t (*restart)(void *ctx); /**< Request an immediate restart. */
} ev_reset_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_RESET_H */
