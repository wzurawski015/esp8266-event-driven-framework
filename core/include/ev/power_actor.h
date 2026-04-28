#ifndef EV_POWER_ACTOR_H
#define EV_POWER_ACTOR_H

#include <stdint.h>

#include "ev/compiler.h"
#include "ev/msg.h"
#include "ev/port_log.h"
#include "ev/system_port.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_POWER_MAX_SLEEP_DURATION_MS (24UL * 60UL * 60UL * 1000UL)
#define EV_POWER_MAX_SLEEP_DURATION_US ((uint64_t)EV_POWER_MAX_SLEEP_DURATION_MS * 1000ULL)

typedef struct {
    uint32_t duration_ms;
} ev_sys_goto_sleep_cmd_t;

EV_STATIC_ASSERT(sizeof(ev_sys_goto_sleep_cmd_t) <= EV_MSG_INLINE_CAPACITY,
                 "Deep sleep command payload must fit in one inline event payload");

typedef enum ev_power_sleep_reject_reason {
    EV_POWER_SLEEP_REJECT_NONE = 0,
    EV_POWER_SLEEP_REJECT_UNSUPPORTED = 1,
    EV_POWER_SLEEP_REJECT_BAD_PAYLOAD = 2,
    EV_POWER_SLEEP_REJECT_BAD_DURATION = 3,
    EV_POWER_SLEEP_REJECT_NOT_QUIESCENT = 4,
    EV_POWER_SLEEP_REJECT_LOG_FLUSH_FAILED = 5,
    EV_POWER_SLEEP_REJECT_PREPARE_FAILED = 6,
    EV_POWER_SLEEP_REJECT_DEEP_SLEEP_FAILED = 7,
    EV_POWER_SLEEP_REJECT_ARMING_FAILED = 8
} ev_power_sleep_reject_reason_t;

typedef struct ev_power_quiescence_report {
    ev_power_sleep_reject_reason_t reason;
    uint32_t pending_actor_messages;
    uint32_t pending_irq_samples;
    uint32_t due_timer_count;
    uint32_t pending_oled_flush;
    uint32_t pending_ds18b20_conversion;
} ev_power_quiescence_report_t;

typedef ev_result_t (*ev_power_quiescence_guard_fn_t)(void *ctx,
                                                       uint64_t duration_us,
                                                       ev_power_quiescence_report_t *out_report);

typedef ev_result_t (*ev_power_sleep_arm_fn_t)(void *ctx,
                                               uint64_t duration_us,
                                               ev_power_quiescence_report_t *out_report);
typedef ev_result_t (*ev_power_sleep_disarm_fn_t)(void *ctx);

typedef struct {
    ev_system_port_t *system_port;
    ev_log_port_t *log_port;
    const char *log_tag;
    ev_power_quiescence_guard_fn_t quiescence_guard;
    void *quiescence_guard_ctx;
    ev_power_sleep_arm_fn_t sleep_arm;
    ev_power_sleep_disarm_fn_t sleep_disarm;
    void *sleep_arming_ctx;
    uint32_t sleep_requests_seen;
    uint32_t sleep_requests_accepted;
    uint32_t sleep_requests_unsupported;
    uint32_t sleep_requests_rejected;
    uint32_t prepare_for_sleep_failures;
    uint32_t sleep_quiescence_failures;
    uint32_t sleep_arming_failures;
    uint32_t sleep_disarm_calls;
    uint32_t sleep_disarm_failures;
    uint32_t log_flush_failures;
    uint32_t deep_sleep_failures;
    uint32_t bad_payload_failures;
    uint32_t bad_duration_failures;
    uint64_t last_duration_us;
    ev_power_sleep_reject_reason_t last_reject_reason;
    ev_power_quiescence_report_t last_quiescence_report;
} ev_power_actor_ctx_t;

ev_result_t ev_power_actor_init(ev_power_actor_ctx_t *ctx,
                                ev_system_port_t *system_port,
                                ev_log_port_t *log_port,
                                const char *log_tag);

ev_result_t ev_power_actor_set_quiescence_guard(ev_power_actor_ctx_t *ctx,
                                                ev_power_quiescence_guard_fn_t guard,
                                                void *guard_ctx);

ev_result_t ev_power_actor_set_sleep_arming(ev_power_actor_ctx_t *ctx,
                                            ev_power_sleep_arm_fn_t arm,
                                            ev_power_sleep_disarm_fn_t disarm,
                                            void *arming_ctx);

ev_result_t ev_power_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_POWER_ACTOR_H */
