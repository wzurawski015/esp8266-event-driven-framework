#include "ev/power_manager.h"

#include <string.h>

#include "ev/fault_bus.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_graph.h"

void ev_power_manager_init(ev_power_manager_t *manager, const ev_power_policy_t *policy)
{
    if (manager != NULL) {
        (void)memset(manager, 0, sizeof(*manager));
        if (policy != NULL) {
            manager->policy = *policy;
        }
    }
}

ev_result_t ev_power_manager_can_sleep_at(ev_runtime_graph_t *graph, ev_power_manager_t *manager, uint32_t now_ms, uint32_t requested_sleep_ms, const ev_quiescence_policy_t *quiescence_policy, ev_quiescence_report_t *out_report)
{
    ev_result_t rc;

    if ((graph == NULL) || (manager == NULL) || (out_report == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((manager->policy.timed_sleep_allowed == 0U) ||
        (requested_sleep_ms < manager->policy.min_sleep_ms) ||
        (requested_sleep_ms > manager->policy.max_sleep_ms)) {
        manager->sleep_rejected++;
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_SLEEP_REJECTED, 1U);
        return EV_ERR_POLICY;
    }

    rc = ev_runtime_is_quiescent_at(graph, now_ms, quiescence_policy, out_report);
    if (rc != EV_OK) {
        ev_fault_payload_t fault;
        (void)memset(&fault, 0, sizeof(fault));
        fault.fault_id = EV_FAULT_SLEEP_REJECTED;
        fault.severity = EV_FAULT_SEV_INFO;
        fault.source_actor = ACT_POWER;
        fault.triggering_event = EV_SYS_GOTO_SLEEP_CMD;
        (void)ev_fault_emit(&graph->faults, &fault);
        manager->sleep_rejected++;
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_SLEEP_REJECTED, 1U);
        return rc;
    }
    manager->sleep_accepted++;
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_SLEEP_ACCEPTED, 1U);
    return EV_OK;
}


ev_result_t ev_power_manager_can_sleep(ev_runtime_graph_t *graph, ev_power_manager_t *manager, uint32_t requested_sleep_ms, ev_quiescence_report_t *out_report)
{
    return ev_power_manager_can_sleep_at(graph, manager, 0U, requested_sleep_ms, NULL, out_report);
}
