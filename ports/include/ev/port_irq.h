#ifndef EV_PORT_IRQ_H
#define EV_PORT_IRQ_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stable logical identifier of one adapter-owned interrupt line.
 */
typedef uint8_t ev_irq_line_id_t;

/**
 * @brief Normalized interrupt edge classification.
 */
typedef uint8_t ev_irq_edge_t;

#define EV_IRQ_EDGE_RISING ((ev_irq_edge_t)1U)
#define EV_IRQ_EDGE_FALLING ((ev_irq_edge_t)2U)

/**
 * @brief One interrupt sample popped from an adapter-owned ring buffer.
 *
 * The contract intentionally exposes only normalized logical data required by
 * the portable runtime. Concrete platform adapters remain responsible for GPIO
 * numbering, ISR bookkeeping, edge filtering, and timestamp capture.
 */
typedef struct {
    uint8_t line_id;
    ev_irq_edge_t edge;
    uint8_t level;
    uint32_t timestamp_us;
} ev_irq_sample_t;

/**
 * @brief Pop one pending interrupt sample from the adapter buffer.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param out_sample Destination sample populated on success.
 * @return EV_OK when one sample was returned, EV_ERR_EMPTY when no sample is
 *         pending, or another error code when the adapter is not usable.
 */
typedef ev_result_t (*ev_irq_pop_fn_t)(void *ctx, ev_irq_sample_t *out_sample);

/**
 * @brief Enable or disable one logical IRQ ingress line owned by the adapter.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param line_id Stable logical line identifier to modify.
 * @param enabled True to arm the line, false to mask it.
 * @return EV_OK on success or another error code when the adapter cannot honor
 *         the request.
 */
typedef ev_result_t (*ev_irq_enable_fn_t)(void *ctx, ev_irq_line_id_t line_id, bool enabled);

/**
 * @brief Wait until one interrupt sample becomes available or the bounded timeout expires.
 *
 * Implementations may return early when an interrupt sample is enqueued before the
 * timeout expires. Passing a timeout of zero performs a non-blocking readiness check.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param timeout_ms Maximum wait time in milliseconds.
 * @param out_woken Set to true when at least one interrupt sample is available, false
 *        when the timeout elapsed without new samples.
 * @return EV_OK on success or another error code when the adapter cannot honor the wait.
 */
typedef ev_result_t (*ev_irq_wait_fn_t)(void *ctx, uint32_t timeout_ms, bool *out_woken);

/**
 * @brief Bounded IRQ ingress statistics exposed through the portable port.
 *
 * The counters are monotonic best-effort diagnostics. They are intended for
 * observability, HIL gates, and sleep-quiescence checks; callers must not use
 * them as an ordering primitive.
 */
typedef struct ev_irq_stats {
    uint32_t write_seq; /**< Monotonic number of samples accepted into the ring. */
    uint32_t read_seq; /**< Monotonic number of samples popped from the ring. */
    uint32_t pending_samples; /**< Currently pending samples in the adapter ring. */
    uint32_t dropped_samples; /**< Samples dropped because the adapter ring was full. */
    uint32_t high_watermark; /**< Maximum observed pending depth of the adapter ring. */
} ev_irq_stats_t;

/**
 * @brief Copy a stable snapshot of IRQ ingress statistics.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param out_stats Destination statistics snapshot.
 * @return EV_OK on success or another error code when statistics are unavailable.
 */
typedef ev_result_t (*ev_irq_get_stats_fn_t)(void *ctx, ev_irq_stats_t *out_stats);

/**
 * @brief Platform interrupt-ingress contract.
 */
typedef struct ev_irq_port {
    void *ctx;
    ev_irq_pop_fn_t pop;
    ev_irq_enable_fn_t enable;
    ev_irq_wait_fn_t wait;
    ev_irq_get_stats_fn_t get_stats;
} ev_irq_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_IRQ_H */
