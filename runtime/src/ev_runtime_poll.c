#include "ev/runtime_poll.h"

#include <string.h>

static ev_result_t ev_runtime_timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_runtime_graph_t *graph = (ev_runtime_graph_t *)ctx;
    return ev_actor_registry_delivery(target_actor, msg, &graph->registry);
}

ev_result_t ev_runtime_poll_once(ev_runtime_graph_t *graph, uint32_t now_ms, size_t actor_budget, ev_runtime_poll_report_t *out_report)
{
    size_t i;
    ev_runtime_poll_report_t local;

    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(&local, 0, sizeof(local));
    local.last_result = EV_OK;
    local.timers_published = ev_timer_publish_due(&graph->timer_service, now_ms, ev_runtime_timer_delivery, graph, EV_TIMER_SERVICE_CAPACITY);

    for (i = 0U; (i < (size_t)EV_ACTOR_COUNT) && (local.actors_pumped < actor_budget); ++i) {
        if (graph->actor_enabled[i] != 0U) {
            ev_actor_pump_report_t pump;
            ev_result_t rc = ev_actor_runtime_pump_default(&graph->actor_runtimes[i], &pump);
            if (rc != EV_ERR_EMPTY) {
                local.actors_pumped++;
                local.messages_processed += pump.processed;
                local.last_result = rc;
            }
        }
    }

    if (out_report != NULL) {
        *out_report = local;
    }
    return EV_OK;
}
