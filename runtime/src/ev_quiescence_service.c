#include "ev/runtime_graph.h"

#include <string.h>

void ev_quiescence_service_init(ev_quiescence_service_t *svc)
{
    if (svc != NULL) {
        (void)memset(svc, 0, sizeof(*svc));
    }
}

static void ev_quiescence_report_clear(ev_quiescence_report_t *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->reason = "quiescent";
    }
}

ev_result_t ev_runtime_next_wake_deadline_ms(ev_runtime_graph_t *graph, uint32_t *out_deadline_ms)
{
    if ((graph == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_next_deadline_ms(&graph->timer_service, out_deadline_ms);
}

ev_result_t ev_runtime_is_quiescent(ev_runtime_graph_t *graph, ev_quiescence_report_t *out_report)
{
    size_t i;

    if ((graph == NULL) || (out_report == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_quiescence_report_clear(out_report);
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (graph->actor_enabled[i] != 0U) {
            size_t pending = ev_mailbox_count(&graph->mailboxes[i]);
            if (pending > 0U) {
                out_report->pending_actor_messages += (uint32_t)pending;
                if (i < 32U) {
                    out_report->busy_actor_mask |= (1UL << i);
                }
            }
        }
    }

    out_report->pending_ingress_events = (uint32_t)ev_ingress_pending(&graph->ingress_service);
    out_report->due_timers = 0U;
    out_report->pending_trace_records = (uint32_t)ev_trace_pending(&graph->trace_ring);
    out_report->pending_fault_records = (uint32_t)ev_fault_pending_count(&graph->faults);
    if (ev_timer_next_deadline_ms(&graph->timer_service, &out_report->next_deadline_ms) != EV_OK) {
        out_report->next_deadline_ms = 0U;
    }

    if (out_report->pending_actor_messages != 0U) {
        out_report->reason = "actor mailbox pending";
    } else if (out_report->pending_ingress_events != 0U) {
        out_report->reason = "ingress pending";
    } else if (out_report->sleep_blocker_actor_mask != 0U) {
        out_report->reason = "actor sleep blocker";
    } else {
        graph->quiescence_service.accepted++;
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_QUIESCENCE_ACCEPTED, 1U);
        return EV_OK;
    }

    graph->quiescence_service.rejected++;
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_QUIESCENCE_REJECTED, 1U);
    return EV_ERR_NOT_READY;
}
