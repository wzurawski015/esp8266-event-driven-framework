#ifndef EV_RUNTIME_GRAPH_INSPECTION_H
#define EV_RUNTIME_GRAPH_INSPECTION_H

#include <stddef.h>
#include <stdint.h>

#include "ev/active_route_table.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t actor_count;
    size_t pending_actor_messages;
    size_t pending_ingress_events;
    size_t pending_timers;
    uint32_t faults_emitted;
    uint32_t metrics_post_ok;
} ev_runtime_graph_stats_t;

const ev_active_route_table_t *ev_runtime_graph_route_table(const ev_runtime_graph_t *graph);
size_t ev_runtime_graph_configured_mailbox_slots(void);
size_t ev_runtime_graph_configured_mailbox_bytes(void);
uint32_t ev_runtime_graph_metric_value(const ev_runtime_graph_t *graph, ev_metric_id_t metric_id);
size_t ev_runtime_graph_system_pump_bound_count(const ev_runtime_graph_t *graph);
uint32_t ev_runtime_graph_scheduler_poll_count(const ev_runtime_graph_t *graph);
size_t ev_runtime_graph_scheduler_pending(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_poll_scheduler_once(ev_runtime_graph_t *graph, size_t turn_budget, ev_system_pump_report_t *out_report);
const ev_system_pump_stats_t *ev_runtime_graph_system_pump_stats(const ev_runtime_graph_t *graph);
const ev_domain_pump_stats_t *ev_runtime_graph_domain_pump_stats(const ev_runtime_graph_t *graph, ev_execution_domain_t domain);
size_t ev_runtime_graph_domain_pending(const ev_runtime_graph_t *graph, ev_execution_domain_t domain);
ev_runtime_graph_stats_t ev_runtime_graph_stats(const ev_runtime_graph_t *graph);

#ifdef __cplusplus
}
#endif

#endif /* EV_RUNTIME_GRAPH_INSPECTION_H */
