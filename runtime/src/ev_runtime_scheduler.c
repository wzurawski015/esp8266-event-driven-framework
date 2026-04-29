#include "ev/runtime_scheduler.h"

#include <string.h>

ev_result_t ev_runtime_scheduler_init(ev_runtime_scheduler_t *scheduler, ev_actor_registry_t *registry, uint32_t active_domain_mask)
{
    uint32_t domain;
    ev_result_t rc;

    if ((scheduler == NULL) || (registry == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(scheduler, 0, sizeof(*scheduler));
    scheduler->active_domain_mask = active_domain_mask;
    rc = ev_system_pump_init(&scheduler->system);
    if (rc != EV_OK) {
        return rc;
    }

    for (domain = 0U; domain < (uint32_t)EV_DOMAIN_COUNT; ++domain) {
        if ((active_domain_mask == 0U) || ((active_domain_mask & EV_RUNTIME_DOMAIN_MASK(domain)) != 0U)) {
            rc = ev_domain_pump_init(&scheduler->domains[domain], registry, (ev_execution_domain_t)domain);
            if (rc != EV_OK) {
                return rc;
            }
            rc = ev_system_pump_bind(&scheduler->system, &scheduler->domains[domain]);
            if (rc != EV_OK) {
                return rc;
            }
        }
    }
    return EV_OK;
}

ev_result_t ev_runtime_scheduler_poll_once(ev_runtime_scheduler_t *scheduler, size_t turn_budget, ev_system_pump_report_t *out_report)
{
    ev_result_t rc;

    if (scheduler == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    scheduler->poll_calls++;
    rc = ev_system_pump_run(&scheduler->system, turn_budget, out_report);
    if ((out_report != NULL) && (out_report->exhausted_turn_budget != 0)) {
        scheduler->partial_returns++;
        return EV_ERR_PARTIAL;
    }
    return rc;
}

size_t ev_runtime_scheduler_pending(const ev_runtime_scheduler_t *scheduler)
{
    return (scheduler != NULL) ? ev_system_pump_pending(&scheduler->system) : 0U;
}
