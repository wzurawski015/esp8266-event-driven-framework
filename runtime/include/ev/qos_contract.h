#ifndef EV_QOS_CONTRACT_H
#define EV_QOS_CONTRACT_H

#include <stdint.h>
#include "ev/result.h"
#include "ev/route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ev_actor_module_descriptor;

typedef enum ev_qos_failure_behavior {
    EV_QOS_FAILURE_STRICT = 0,
    EV_QOS_FAILURE_DROP_ALLOWED = 1,
    EV_QOS_FAILURE_COALESCE = 2,
    EV_QOS_FAILURE_REPLACE_LATEST = 3
} ev_qos_failure_behavior_t;

typedef struct ev_qos_contract {
    ev_route_qos_t qos;
    ev_qos_failure_behavior_t failure_behavior;
    uint8_t requires_reliable_delivery;
    uint8_t allows_drop;
    uint8_t allows_coalesce;
    uint8_t allows_latest_replace;
    uint8_t wakeup_relevant;
    uint8_t command_relevant;
    uint8_t telemetry_relevant;
} ev_qos_contract_t;

typedef enum ev_qos_contract_violation {
    EV_QOS_CONTRACT_OK = 0,
    EV_QOS_CONTRACT_INVALID_QOS = 1,
    EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH = 2,
    EV_QOS_CONTRACT_WAKEUP_POLICY_MISMATCH = 3,
    EV_QOS_CONTRACT_COMMAND_POLICY_MISMATCH = 4,
    EV_QOS_CONTRACT_TELEMETRY_POLICY_MISMATCH = 5,
    EV_QOS_CONTRACT_UNPROMOTED_ALGORITHM = 6
} ev_qos_contract_violation_t;

typedef struct ev_qos_contract_report {
    ev_qos_contract_violation_t violation;
    ev_route_qos_t route_qos;
    uint32_t module_route_policy;
    uint8_t accepted;
} ev_qos_contract_report_t;

ev_result_t ev_qos_contract_for(ev_route_qos_t qos, ev_qos_contract_t *out_contract);
ev_qos_failure_behavior_t ev_qos_failure_behavior(ev_route_qos_t qos);
int ev_qos_failure_is_drop_allowed(ev_route_qos_t qos);
int ev_qos_failure_is_strict(ev_route_qos_t qos);
int ev_actor_module_route_policy_accepts_qos(const struct ev_actor_module_descriptor *descriptor, ev_route_qos_t qos);
ev_result_t ev_qos_validate_route_against_module(const ev_route_t *route,
                                                 const struct ev_actor_module_descriptor *descriptor,
                                                 ev_qos_contract_report_t *out_report);
const char *ev_qos_failure_behavior_name(ev_qos_failure_behavior_t behavior);
const char *ev_qos_contract_violation_name(ev_qos_contract_violation_t violation);

#ifdef __cplusplus
}
#endif

#endif /* EV_QOS_CONTRACT_H */
