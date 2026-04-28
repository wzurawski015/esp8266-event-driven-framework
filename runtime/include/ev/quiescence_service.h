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
    uint32_t next_deadline_ms;
    const char *reason;
} ev_quiescence_report_t;

typedef ev_result_t (*ev_actor_quiescence_fn_t)(void *actor_context, ev_quiescence_report_t *report);

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
} ev_quiescence_service_t;

void ev_quiescence_service_init(ev_quiescence_service_t *svc);
ev_result_t ev_runtime_is_quiescent(struct ev_runtime_graph *graph, ev_quiescence_report_t *out_report);
ev_result_t ev_runtime_next_wake_deadline_ms(struct ev_runtime_graph *graph, uint32_t *out_deadline_ms);

#endif
