#ifndef EV_WATCHDOG_ACTOR_H
#define EV_WATCHDOG_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/execution_domain.h"
#include "ev/msg.h"
#include "ev/port_wdt.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_WATCHDOG_MIN_TIMEOUT_MS 1000U
#define EV_WATCHDOG_MAX_TIMEOUT_MS 60000U
#define EV_WATCHDOG_MAX_DOMAIN_SNAPSHOTS ((size_t)EV_DOMAIN_COUNT)

typedef enum ev_watchdog_reject_reason {
    EV_WATCHDOG_REJECT_NONE = 0,
    EV_WATCHDOG_REJECT_UNSUPPORTED = 1,
    EV_WATCHDOG_REJECT_NO_LIVENESS = 2,
    EV_WATCHDOG_REJECT_LIVENESS_ERROR = 3,
    EV_WATCHDOG_REJECT_NOT_PROGRESSING = 4,
    EV_WATCHDOG_REJECT_DOMAIN_STALLED = 5,
    EV_WATCHDOG_REJECT_SLEEP_ARMING = 6,
    EV_WATCHDOG_REJECT_PERMANENT_STALL = 7,
    EV_WATCHDOG_REJECT_FEED_FAILED = 8,
    EV_WATCHDOG_REJECT_BAD_EVENT = 9,
    EV_WATCHDOG_REJECT_NO_SYSTEM_TURN = 10
} ev_watchdog_reject_reason_t;

typedef struct ev_watchdog_domain_snapshot {
    ev_execution_domain_t domain;
    uint32_t pump_calls;
    uint32_t pump_empty_calls;
    uint32_t pump_budget_hits;
    size_t pending_messages;
    ev_result_t last_result;
    bool bound;
} ev_watchdog_domain_snapshot_t;

typedef struct ev_watchdog_liveness_snapshot {
    size_t system_turn_counter;
    size_t system_messages_processed;
    size_t system_pending_messages;
    size_t domain_count;
    ev_watchdog_domain_snapshot_t domains[EV_WATCHDOG_MAX_DOMAIN_SNAPSHOTS];
    bool sleep_arming;
    bool permanent_stall;
} ev_watchdog_liveness_snapshot_t;

typedef ev_result_t (*ev_watchdog_liveness_fn_t)(void *ctx, ev_watchdog_liveness_snapshot_t *out_snapshot);

typedef struct ev_watchdog_actor_stats {
    uint32_t ticks_seen;
    uint32_t feed_attempts;
    uint32_t feeds_ok;
    uint32_t feeds_failed;
    uint32_t health_rejects;
    uint32_t unsupported_rejects;
    uint32_t enable_calls;
    uint32_t enable_failures;
    uint32_t liveness_errors;
    ev_watchdog_reject_reason_t last_reject_reason;
    ev_reset_reason_t last_reset_reason;
} ev_watchdog_actor_stats_t;

typedef struct ev_watchdog_actor_ctx {
    ev_wdt_port_t *wdt_port;
    ev_watchdog_liveness_fn_t liveness_fn;
    void *liveness_ctx;
    uint32_t timeout_ms;
    bool enabled;
    bool supported;
    bool has_last_snapshot;
    ev_watchdog_liveness_snapshot_t last_snapshot;
    ev_watchdog_actor_stats_t stats;
} ev_watchdog_actor_ctx_t;

ev_result_t ev_watchdog_actor_init(
    ev_watchdog_actor_ctx_t *ctx,
    ev_wdt_port_t *wdt_port,
    uint32_t timeout_ms,
    ev_watchdog_liveness_fn_t liveness_fn,
    void *liveness_ctx);

ev_result_t ev_watchdog_actor_handle(void *actor_context, const ev_msg_t *msg);

const ev_watchdog_actor_stats_t *ev_watchdog_actor_stats(const ev_watchdog_actor_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EV_WATCHDOG_ACTOR_H */
