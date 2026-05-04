#include "ev/runtime_poll.h"

#include <string.h>

static ev_result_t ev_runtime_timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_runtime_graph_t *graph = (ev_runtime_graph_t *)ctx;
    return ev_runtime_graph_send(graph, target_actor, msg);
}

ev_result_t ev_runtime_poll_once(ev_runtime_graph_t *graph, uint32_t now_ms, size_t actor_budget, ev_runtime_poll_report_t *out_report)
{
    ev_runtime_poll_report_t local;
    ev_system_pump_report_t system_report;
    ev_result_t rc;

    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(&local, 0, sizeof(local));
    (void)memset(&system_report, 0, sizeof(system_report));
    local.last_result = EV_OK;
    local.timers_published = ev_timer_publish_due(&graph->timer_service, now_ms, ev_runtime_timer_delivery, graph, EV_TIMER_SERVICE_CAPACITY);

    rc = ev_runtime_scheduler_poll_once(&graph->scheduler, actor_budget, &system_report);
    local.domains_pumped = system_report.domains_pumped;
    local.actors_pumped = system_report.turns_processed;
    local.messages_processed = system_report.messages_processed;
    local.last_result = rc;
    if (rc == EV_ERR_EMPTY) {
        local.last_result = EV_OK;
        rc = EV_OK;
    }

    if (out_report != NULL) {
        *out_report = local;
    }
    return rc;
}
