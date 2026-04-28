#ifndef EV_PORT_CLOCK_H
#define EV_PORT_CLOCK_H

#include <stdint.h>

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Monotonic time value expressed in microseconds.
 *
 * The public unit stays in microseconds even when one concrete platform source
 * has a lower effective resolution.
 */
typedef uint64_t ev_time_mono_us_t;

/**
 * @brief Wall-clock time value expressed in microseconds.
 */
typedef uint64_t ev_time_wall_us_t;

/**
 * @brief Platform clock contract.
 */
typedef struct ev_clock_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*mono_now_us)(void *ctx, ev_time_mono_us_t *out_now); /**< Read monotonic time. */
    ev_result_t (*wall_now_us)(void *ctx, ev_time_wall_us_t *out_now); /**< Read wall-clock time. */
    ev_result_t (*delay_ms)(void *ctx, uint32_t delay_ms); /**< Delay execution for at least @p delay_ms. */
} ev_clock_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_CLOCK_H */
