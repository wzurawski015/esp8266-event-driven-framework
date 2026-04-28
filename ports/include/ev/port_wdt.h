#ifndef EV_PORT_WDT_H
#define EV_PORT_WDT_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/port_reset.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Minimal hardware watchdog mechanism contract.
 *
 * The port only exposes platform watchdog mechanism. Runtime health policy is
 * owned by the Watchdog Actor so the platform adapter cannot mask a stalled
 * event graph by feeding the watchdog independently.
 */
typedef struct ev_wdt_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*enable)(void *ctx, uint32_t timeout_ms); /**< Configure/enable the platform watchdog. */
    ev_result_t (*feed)(void *ctx); /**< Feed the watchdog timer. */
    ev_result_t (*get_reset_reason)(void *ctx, ev_reset_reason_t *out_reason); /**< Optional reset reason query. */
    ev_result_t (*is_supported)(void *ctx, bool *out_supported); /**< Optional support probe. */
} ev_wdt_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_WDT_H */
