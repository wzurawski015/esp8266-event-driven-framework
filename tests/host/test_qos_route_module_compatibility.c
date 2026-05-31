#include <assert.h>
#include "ev/qos_contract.h"
#include "ev/actor_module.h"

static ev_route_t make_route(ev_event_id_t event_id, ev_actor_id_t actor, ev_route_qos_t qos)
{
    ev_route_t route = { event_id, actor, qos, 0U, 0U };
    return route;
}

static void expect_route(ev_event_id_t event_id, ev_actor_id_t actor, ev_route_qos_t qos, ev_result_t expected, ev_qos_contract_violation_t violation)
{
    ev_route_t route = make_route(event_id, actor, qos);
    const ev_actor_module_descriptor_t *module = ev_actor_module_find(actor);
    ev_qos_contract_report_t report;
    ev_result_t rc;
    assert(module != NULL);
    rc = ev_qos_validate_route_against_module(&route, module, &report);
    assert(rc == expected);
    assert(report.route_qos == qos);
    assert(report.module_route_policy == module->route_policy_flags);
    assert(report.violation == violation);
    assert(report.accepted == ((expected == EV_OK) ? 1U : 0U));
}

int main(void)
{
    expect_route(EV_SYS_GOTO_SLEEP_CMD, ACT_POWER, EV_ROUTE_QOS_WAKEUP_CRITICAL, EV_OK, EV_QOS_CONTRACT_OK);
    expect_route(EV_NET_MQTT_MSG_RX, ACT_COMMAND, EV_ROUTE_QOS_COMMAND, EV_OK, EV_QOS_CONTRACT_OK);
    expect_route(EV_TIME_UPDATED, ACT_NETWORK, EV_ROUTE_QOS_TELEMETRY, EV_OK, EV_QOS_CONTRACT_OK);
    expect_route(EV_FAULT_REPORTED, ACT_FAULT, EV_ROUTE_QOS_CRITICAL, EV_OK, EV_QOS_CONTRACT_OK);
    expect_route(EV_TIME_UPDATED, ACT_COMMAND, EV_ROUTE_QOS_TELEMETRY, EV_ERR_POLICY, EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH);
    expect_route(EV_NET_MQTT_MSG_RX, ACT_NETWORK, EV_ROUTE_QOS_COMMAND, EV_ERR_POLICY, EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH);
    expect_route(EV_SYS_GOTO_SLEEP_CMD, ACT_NETWORK, EV_ROUTE_QOS_WAKEUP_CRITICAL, EV_ERR_POLICY, EV_QOS_CONTRACT_MODULE_POLICY_MISMATCH);
    expect_route(EV_TICK_1S, ACT_APP, (ev_route_qos_t)99, EV_ERR_OUT_OF_RANGE, EV_QOS_CONTRACT_INVALID_QOS);
    return 0;
}
