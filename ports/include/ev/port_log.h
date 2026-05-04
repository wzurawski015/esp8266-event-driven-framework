#ifndef EV_PORT_LOG_H
#define EV_PORT_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Severity carried by the public logging contract.
 */
typedef enum ev_log_level {
    EV_LOG_DEBUG = 0,
    EV_LOG_INFO = 1,
    EV_LOG_WARN = 2,
    EV_LOG_ERROR = 3,
    EV_LOG_FATAL = 4
} ev_log_level_t;

/**
 * @brief Platform logging contract.
 */
typedef struct ev_log_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*write)(void *ctx,
                         ev_log_level_t level,
                         const char *tag,
                         const char *message,
                         size_t message_len); /**< Emit one log record. */
    ev_result_t (*flush)(void *ctx); /**< Flush any buffered output if needed. */
    ev_result_t (*pending)(void *ctx, uint32_t *out_pending_records); /**< Inspect buffered records without blocking. */
} ev_log_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_LOG_H */
