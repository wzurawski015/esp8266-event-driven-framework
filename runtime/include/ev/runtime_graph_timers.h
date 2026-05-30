#ifndef EV_RUNTIME_GRAPH_TIMERS_H
#define EV_RUNTIME_GRAPH_TIMERS_H

#include <stddef.h>
#include <stdint.h>

#include "ev/runtime_graph.h"
#include "ev/timer_service.h"

#ifdef __cplusplus
extern "C" {
#endif

ev_result_t ev_runtime_graph_schedule_oneshot(ev_runtime_graph_t *graph,
                                              uint32_t now_ms,
                                              uint32_t delay_ms,
                                              ev_actor_id_t target_actor,
                                              ev_event_id_t event_id,
                                              uint32_t arg0,
                                              ev_timer_token_t *out_token);
size_t ev_runtime_graph_timer_pending_count(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_schedule_periodic(ev_runtime_graph_t *graph,
                                               uint32_t now_ms,
                                               uint32_t period_ms,
                                               ev_actor_id_t target_actor,
                                               ev_event_id_t event_id,
                                               uint32_t arg0,
                                               ev_timer_token_t *out_token);
size_t ev_runtime_graph_publish_due_timers(ev_runtime_graph_t *graph,
                                           uint32_t now_ms,
                                           ev_timer_publish_fn_t deliver,
                                           void *deliver_ctx,
                                           size_t max_publish);
uint32_t ev_runtime_graph_timer_published_count(const ev_runtime_graph_t *graph);

#ifdef __cplusplus
}
#endif

#endif /* EV_RUNTIME_GRAPH_TIMERS_H */
