#ifndef EV_RUNTIME_LOOP_H
#define EV_RUNTIME_LOOP_H

#include <stddef.h>
#include <stdint.h>

#include "ev/runtime_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t max_pump_calls;
    size_t max_messages;
    size_t max_turns;
    size_t max_irq_samples;
    size_t max_net_samples;
    size_t timer_publish_budget;
    size_t scheduler_turn_budget;
    uint8_t skip_timers_when_scheduler_pending;
    uint8_t run_scheduler_after_timers;
} ev_runtime_loop_policy_t;

typedef struct {
    uint32_t pump_calls;
    uint32_t turns;
    uint32_t messages;
    uint32_t irq_samples;
    uint32_t net_samples;
    uint32_t timers_published;
    uint32_t pending_before;
    uint32_t pending_after;
    uint32_t elapsed_ms;
    uint8_t exhausted;
    uint8_t partial;
    ev_result_t last_result;
} ev_runtime_loop_report_t;

typedef ev_result_t (*ev_runtime_loop_collect_fn_t)(
    ev_runtime_graph_t *graph,
    void *context,
    ev_runtime_loop_report_t *report,
    const ev_runtime_loop_policy_t *policy);

typedef ev_result_t (*ev_runtime_loop_now_fn_t)(void *context, uint32_t *out_now_ms);

typedef ev_result_t (*ev_runtime_loop_pending_fn_t)(
    ev_runtime_graph_t *graph,
    void *context,
    uint32_t now_ms,
    const ev_runtime_loop_report_t *report,
    uint8_t *out_pending);

typedef struct {
    ev_runtime_loop_collect_fn_t collect_ingress;
    void *collect_ctx;
    ev_timer_delivery_fn_t timer_delivery;
    void *timer_delivery_ctx;
    ev_runtime_loop_now_fn_t now_ms;
    void *now_ctx;
    ev_runtime_loop_pending_fn_t work_pending;
    void *work_pending_ctx;
} ev_runtime_loop_ports_t;

void ev_runtime_loop_policy_default(ev_runtime_loop_policy_t *policy);
void ev_runtime_loop_report_reset(ev_runtime_loop_report_t *report);
ev_result_t ev_runtime_loop_poll_once(
    ev_runtime_graph_t *graph,
    const ev_runtime_loop_policy_t *policy,
    const ev_runtime_loop_ports_t *ports,
    ev_runtime_loop_report_t *out_report);

#ifdef __cplusplus
}
#endif

#endif /* EV_RUNTIME_LOOP_H */
