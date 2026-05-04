#include "ev/quiescence_service.h"

#include <string.h>

#include "ev/active_route_table.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_graph.h"
#include "ev/timer_service.h"

static void ev_quiescence_report_init(ev_quiescence_report_t *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->reason = "ok";
    }
}

static ev_quiescence_policy_t ev_quiescence_default_policy(void)
{
    ev_quiescence_policy_t policy;
    (void)memset(&policy, 0, sizeof(policy));
    policy.trace_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.fault_policy = EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY;
    policy.log_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.block_due_timers = 1U;
    policy.block_actor_sleep_blockers = 1U;
    return policy;
}

static uint32_t ev_quiescence_due_timer_count(const ev_timer_service_t *svc, uint32_t now_ms)
{
    uint32_t count = 0U;
    size_t i;

    if (svc == NULL) {
        return 0U;
    }
    for (i = 0U; i < EV_TIMER_SERVICE_CAPACITY; ++i) {
        if ((svc->slots[i].active != 0U) && (ev_timer_is_due(now_ms, svc->slots[i].deadline_ms) != 0)) {
            count++;
        }
    }
    return count;
}

static int ev_fault_has_critical(const ev_fault_registry_t *registry)
{
    size_t i;
    if (registry == NULL) {
        return 0;
    }
    for (i = 0U; i < registry->count; ++i) {
        if (registry->records[i].severity == EV_FAULT_SEV_CRITICAL) {
            return 1;
        }
    }
    return 0;
}

void ev_quiescence_service_init(ev_quiescence_service_t *svc)
{
    if (svc != NULL) {
        (void)memset(svc, 0, sizeof(*svc));
    }
}

ev_result_t ev_runtime_is_quiescent_at(ev_runtime_graph_t *graph, uint32_t now_ms, const ev_quiescence_policy_t *policy, ev_quiescence_report_t *out_report)
{
    ev_quiescence_report_t report;
    ev_quiescence_policy_t effective_policy;
    size_t i;
    uint32_t next_deadline = 0U;
    ev_result_t deadline_rc;

    if ((graph == NULL) || (out_report == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_quiescence_report_init(&report);
    effective_policy = (policy != NULL) ? *policy : ev_quiescence_default_policy();

    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (graph->actor_enabled[i] != 0U) {
            report.pending_actor_messages += (uint32_t)ev_mailbox_count(&graph->mailboxes[i]);
            if ((graph->instance_bound[i] != 0U) && (graph->instances[i].quiescence_fn != NULL)) {
                (void)graph->instances[i].quiescence_fn(graph->actor_runtimes[i].actor_context, &report);
            } else if ((graph->descriptors[i] != NULL) && (graph->descriptors[i]->quiescence_fn != NULL)) {
                (void)graph->descriptors[i]->quiescence_fn(graph->actor_runtimes[i].actor_context, &report);
            }
        }
    }
    report.pending_ingress_events = (uint32_t)ev_ingress_pending(&graph->ingress_service);
    report.due_timers = ev_quiescence_due_timer_count(&graph->timer_service, now_ms);
    report.pending_trace_records = (uint32_t)ev_trace_pending(&graph->trace_ring);
    report.pending_fault_records = (uint32_t)ev_fault_pending_count(&graph->faults);
    report.pending_log_records = 0U;
    if ((graph->ports.log != NULL) && (graph->ports.log->pending != NULL)) {
        uint32_t pending_logs = 0U;
        if (graph->ports.log->pending(graph->ports.log->ctx, &pending_logs) == EV_OK) {
            report.pending_log_records = pending_logs;
        }
    }

    deadline_rc = ev_timer_next_deadline_ms(&graph->timer_service, &next_deadline);
    if (deadline_rc == EV_OK) {
        report.next_deadline_ms = next_deadline;
        report.earliest_safe_sleep_until_ms = next_deadline;
        if ((effective_policy.near_deadline_guard_ms > 0U) && ((uint32_t)(next_deadline - now_ms) <= effective_policy.near_deadline_guard_ms)) {
            report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_NEAR_DEADLINE;
        }
        if ((effective_policy.min_sleep_window_ms > 0U) && ((uint32_t)(next_deadline - now_ms) < effective_policy.min_sleep_window_ms)) {
            report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_NEAR_DEADLINE;
        }
    }

    if ((effective_policy.block_due_timers != 0U) && (report.due_timers > 0U)) {
        report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_DUE_TIMER;
    }
    if ((effective_policy.block_actor_sleep_blockers != 0U) && (report.sleep_blocker_actor_mask != 0U)) {
        report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_ACTOR;
    }
    if ((effective_policy.trace_policy == EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED) && (report.pending_trace_records > 0U)) {
        report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_TRACE;
    }
    if (((effective_policy.fault_policy == EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED) && (report.pending_fault_records > 0U)) ||
        ((effective_policy.fault_policy == EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY) && (ev_fault_has_critical(&graph->faults) != 0))) {
        report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_FAULT;
    }
    if ((effective_policy.log_policy == EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED) && (report.pending_log_records > 0U)) {
        report.policy_blocker_mask |= EV_QUIESCENCE_POLICY_BLOCK_LOG;
    }

    if (report.pending_actor_messages > 0U) {
        report.reason = "actor messages pending";
    } else if (report.pending_ingress_events > 0U) {
        report.reason = "ingress pending";
    } else if (report.policy_blocker_mask != 0U) {
        report.reason = "policy blocker";
    }

    *out_report = report;
    if ((report.pending_actor_messages == 0U) &&
        (report.pending_ingress_events == 0U) &&
        (report.policy_blocker_mask == 0U)) {
        graph->quiescence_service.accepted++;
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_QUIESCENCE_ACCEPTED, 1U);
        return EV_OK;
    }

    graph->quiescence_service.rejected++;
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_QUIESCENCE_REJECTED, 1U);
    return EV_ERR_NOT_READY;
}

ev_result_t ev_runtime_is_quiescent(ev_runtime_graph_t *graph, ev_quiescence_report_t *out_report)
{
    ev_quiescence_policy_t policy = ev_quiescence_default_policy();
    return ev_runtime_is_quiescent_at(graph, 0U, &policy, out_report);
}

ev_result_t ev_runtime_next_wake_deadline_ms(ev_runtime_graph_t *graph, uint32_t *out_deadline_ms)
{
    if ((graph == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_next_deadline_ms(&graph->timer_service, out_deadline_ms);
}
