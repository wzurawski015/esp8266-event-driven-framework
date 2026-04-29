#ifndef EV_QUIESCENCE_SERVICE_H
#define EV_QUIESCENCE_SERVICE_H

#include <stdint.h>
#include "ev/result.h"

struct ev_runtime_graph;

typedef struct {
    uint32_t pending_actor_messages;
    uint32_t pending_ingress_events;
    uint32_t due_timers;
    uint32_t busy_actor_mask;
    uint32_t sleep_blocker_actor_mask;
    uint32_t pending_trace_records;
    uint32_t pending_fault_records;
    uint32_t pending_log_records;
    uint32_t next_deadline_ms;
    uint32_t earliest_safe_sleep_until_ms;
    uint32_t policy_blocker_mask;
    const char *reason;
} ev_quiescence_report_t;

typedef ev_result_t (*ev_actor_quiescence_fn_t)(void *actor_context, ev_quiescence_report_t *report);

typedef enum {
    EV_QUIESCENCE_BUFFER_BLOCK_NEVER = 0,
    EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED,
    EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY
} ev_quiescence_buffer_policy_t;

typedef struct {
    ev_quiescence_buffer_policy_t trace_policy;
    ev_quiescence_buffer_policy_t fault_policy;
    ev_quiescence_buffer_policy_t log_policy;
    uint32_t min_sleep_window_ms;
    uint32_t near_deadline_guard_ms;
    uint8_t block_due_timers;
    uint8_t block_actor_sleep_blockers;
} ev_quiescence_policy_t;

#define EV_QUIESCENCE_POLICY_BLOCK_DUE_TIMER 0x00000001UL
#define EV_QUIESCENCE_POLICY_BLOCK_ACTOR 0x00000002UL
#define EV_QUIESCENCE_POLICY_BLOCK_TRACE 0x00000004UL
#define EV_QUIESCENCE_POLICY_BLOCK_FAULT 0x00000008UL
#define EV_QUIESCENCE_POLICY_BLOCK_LOG 0x00000010UL
#define EV_QUIESCENCE_POLICY_BLOCK_NEAR_DEADLINE 0x00000020UL

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
} ev_quiescence_service_t;

void ev_quiescence_service_init(ev_quiescence_service_t *svc);
ev_result_t ev_runtime_is_quiescent_at(struct ev_runtime_graph *graph, uint32_t now_ms, const ev_quiescence_policy_t *policy, ev_quiescence_report_t *out_report);
ev_result_t ev_runtime_is_quiescent(struct ev_runtime_graph *graph, ev_quiescence_report_t *out_report);
ev_result_t ev_runtime_next_wake_deadline_ms(struct ev_runtime_graph *graph, uint32_t *out_deadline_ms);

#endif
