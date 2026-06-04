#include "ev/qos_contract.h"

#include <string.h>

#include "ev/active_route_table.h"
#include "ev/actor_module.h"

static void ev_qos_report_reset(ev_qos_contract_report_t *report, ev_route_qos_t qos, uint32_t policy)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->route_qos = qos;
        report->module_route_policy = policy;
        report->violation = EV_QOS_CONTRACT_OK;
    }
}

const char *ev_qos_failure_behavior_name(ev_qos_failure_behavior_t behavior)
{
    switch (behavior) {
    case EV_QOS_FAILURE_STRICT: return "strict";
    case EV_QOS_FAILURE_DROP_ALLOWED: return "drop_allowed";
    case EV_QOS_FAILURE_COALESCE: return "coalesce";
    case EV_QOS_FAILURE_REPLACE_LATEST: return "replace_latest";
    default: return "unknown";
    }
}

const char *ev_qos_contract_violation_name(ev_qos_contract_violation_t violation)
{
    switch (violation) {
    case EV_QOS_CONTRACT_OK: return "ok";
    case EV_QOS_CONTRACT_INVALID_QOS: return "invalid_qos";
    case EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH: return "module_policy_mismatch";
    case EV_QOS_CONTRACT_WAKEUP_POLICY_MISMATCH: return "wakeup_policy_mismatch";
    case EV_QOS_CONTRACT_COMMAND_POLICY_MISMATCH: return "command_policy_mismatch";
    case EV_QOS_CONTRACT_TELEMETRY_POLICY_MISMATCH: return "telemetry_policy_mismatch";
    case EV_QOS_CONTRACT_UNPROMOTED_ALGORITHM: return "unpromoted_algorithm";
    default: return "unknown";
    }
}

ev_result_t ev_qos_contract_for(ev_route_qos_t qos, ev_qos_contract_t *out_contract)
{
    ev_qos_contract_t c;
    if (out_contract == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(&c, 0, sizeof(c));
    c.qos = qos;
    switch (qos) {
    case EV_ROUTE_QOS_CRITICAL:
        c.failure_behavior = EV_QOS_FAILURE_STRICT; c.requires_reliable_delivery = 1U; break;
    case EV_ROUTE_QOS_WAKEUP_CRITICAL:
        c.failure_behavior = EV_QOS_FAILURE_STRICT; c.requires_reliable_delivery = 1U; c.wakeup_relevant = 1U; break;
    case EV_ROUTE_QOS_COMMAND:
        c.failure_behavior = EV_QOS_FAILURE_STRICT; c.requires_reliable_delivery = 1U; c.command_relevant = 1U; break;
    case EV_ROUTE_QOS_BEST_EFFORT:
    case EV_ROUTE_QOS_LOSSY:
        c.failure_behavior = EV_QOS_FAILURE_DROP_ALLOWED; c.allows_drop = 1U; break;
    case EV_ROUTE_QOS_TELEMETRY:
        c.failure_behavior = EV_QOS_FAILURE_DROP_ALLOWED; c.allows_drop = 1U; c.telemetry_relevant = 1U; break;
    case EV_ROUTE_QOS_COALESCED:
        c.failure_behavior = EV_QOS_FAILURE_COALESCE; c.allows_drop = 1U; c.allows_coalesce = 1U; break;
    case EV_ROUTE_QOS_LATEST_ONLY:
        c.failure_behavior = EV_QOS_FAILURE_REPLACE_LATEST; c.allows_drop = 1U; c.allows_latest_replace = 1U; break;
    default:
        return EV_ERR_OUT_OF_RANGE;
    }
    *out_contract = c;
    return EV_OK;
}

ev_qos_failure_behavior_t ev_qos_failure_behavior(ev_route_qos_t qos)
{
    ev_qos_contract_t contract;
    if (ev_qos_contract_for(qos, &contract) != EV_OK) {
        return EV_QOS_FAILURE_STRICT;
    }
    return contract.failure_behavior;
}

int ev_qos_failure_is_drop_allowed(ev_route_qos_t qos)
{
    return ev_qos_failure_behavior(qos) != EV_QOS_FAILURE_STRICT;
}

int ev_qos_failure_is_strict(ev_route_qos_t qos)
{
    return ev_qos_failure_behavior(qos) == EV_QOS_FAILURE_STRICT;
}

int ev_actor_module_route_policy_accepts_qos(const ev_actor_module_descriptor_t *descriptor, ev_route_qos_t qos)
{
    uint32_t policy;
    if ((descriptor == NULL) || (ev_route_qos_is_valid(qos) == 0)) {
        return 0;
    }
    policy = descriptor->route_policy_flags;
    if (policy == 0U || qos == (ev_route_qos_t)policy) {
        return 1;
    }
    if ((policy == EV_ROUTE_QOS_WAKEUP_CRITICAL) && (qos == EV_ROUTE_QOS_CRITICAL)) {
        return 1;
    }
    if ((policy == EV_ROUTE_QOS_TELEMETRY) && ((qos == EV_ROUTE_QOS_TELEMETRY) || (qos == EV_ROUTE_QOS_BEST_EFFORT) || (qos == EV_ROUTE_QOS_LOSSY) || (qos == EV_ROUTE_QOS_CRITICAL))) {
        return 1;
    }
    if ((policy == EV_ROUTE_QOS_COMMAND) && ((qos == EV_ROUTE_QOS_COMMAND) || (qos == EV_ROUTE_QOS_CRITICAL))) {
        return 1;
    }
    return 0;
}

ev_result_t ev_qos_validate_route_against_module(const ev_route_t *route,
                                                 const ev_actor_module_descriptor_t *descriptor,
                                                 ev_qos_contract_report_t *out_report)
{
    ev_qos_contract_t contract;
    uint32_t policy = (descriptor != NULL) ? descriptor->route_policy_flags : 0U;
    if ((route == NULL) || (descriptor == NULL)) {
        ev_qos_report_reset(out_report, EV_ROUTE_QOS_CRITICAL, policy);
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH; }
        return EV_ERR_INVALID_ARG;
    }
    ev_qos_report_reset(out_report, route->qos, policy);
    if (ev_qos_contract_for(route->qos, &contract) != EV_OK) {
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_INVALID_QOS; }
        return EV_ERR_OUT_OF_RANGE;
    }
    if (ev_actor_module_route_policy_accepts_qos(descriptor, route->qos) == 0) {
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH; }
        return EV_ERR_POLICY;
    }
    if ((contract.wakeup_relevant != 0U) && (policy != EV_ROUTE_QOS_WAKEUP_CRITICAL)) {
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_WAKEUP_POLICY_MISMATCH; }
        return EV_ERR_POLICY;
    }
    if ((contract.command_relevant != 0U) && (policy != EV_ROUTE_QOS_COMMAND)) {
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_COMMAND_POLICY_MISMATCH; }
        return EV_ERR_POLICY;
    }
    if ((contract.telemetry_relevant != 0U) && (policy != EV_ROUTE_QOS_TELEMETRY)) {
        if (out_report != NULL) { out_report->violation = EV_QOS_CONTRACT_TELEMETRY_POLICY_MISMATCH; }
        return EV_ERR_POLICY;
    }
    if (out_report != NULL) { out_report->accepted = 1U; }
    return EV_OK;
}
