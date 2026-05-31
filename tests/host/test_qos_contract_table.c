#include <assert.h>
#include <string.h>
#include "ev/qos_contract.h"
#include "ev/actor_module.h"

static void expect_contract(ev_route_qos_t qos, ev_qos_failure_behavior_t behavior, int reliable, int drop, int wakeup, int command, int telemetry)
{
    ev_qos_contract_t c;
    assert(ev_qos_contract_for(qos, &c) == EV_OK);
    assert(c.qos == qos);
    assert(c.failure_behavior == behavior);
    assert(c.requires_reliable_delivery == (uint8_t)reliable);
    assert(c.allows_drop == (uint8_t)drop);
    assert(c.wakeup_relevant == (uint8_t)wakeup);
    assert(c.command_relevant == (uint8_t)command);
    assert(c.telemetry_relevant == (uint8_t)telemetry);
}

int main(void)
{
    expect_contract(EV_ROUTE_QOS_CRITICAL, EV_QOS_FAILURE_STRICT, 1, 0, 0, 0, 0);
    expect_contract(EV_ROUTE_QOS_WAKEUP_CRITICAL, EV_QOS_FAILURE_STRICT, 1, 0, 1, 0, 0);
    expect_contract(EV_ROUTE_QOS_COMMAND, EV_QOS_FAILURE_STRICT, 1, 0, 0, 1, 0);
    expect_contract(EV_ROUTE_QOS_BEST_EFFORT, EV_QOS_FAILURE_DROP_ALLOWED, 0, 1, 0, 0, 0);
    expect_contract(EV_ROUTE_QOS_LOSSY, EV_QOS_FAILURE_DROP_ALLOWED, 0, 1, 0, 0, 0);
    expect_contract(EV_ROUTE_QOS_TELEMETRY, EV_QOS_FAILURE_DROP_ALLOWED, 0, 1, 0, 0, 1);
    expect_contract(EV_ROUTE_QOS_COALESCED, EV_QOS_FAILURE_DROP_ALLOWED, 0, 1, 0, 0, 0);
    expect_contract(EV_ROUTE_QOS_LATEST_ONLY, EV_QOS_FAILURE_DROP_ALLOWED, 0, 1, 0, 0, 0);
    assert(ev_qos_contract_for((ev_route_qos_t)99, &(ev_qos_contract_t){0}) == EV_ERR_OUT_OF_RANGE);
    assert(ev_qos_failure_is_drop_allowed(EV_ROUTE_QOS_TELEMETRY) != 0);
    assert(ev_qos_failure_is_strict(EV_ROUTE_QOS_COMMAND) != 0);
    assert(strcmp(ev_qos_failure_behavior_name(EV_QOS_FAILURE_STRICT), "strict") == 0);
    assert(ev_actor_module_route_policy_accepts_qos(ev_actor_module_find(ACT_POWER), EV_ROUTE_QOS_WAKEUP_CRITICAL) != 0);
    assert(ev_actor_module_route_policy_accepts_qos(ev_actor_module_find(ACT_NETWORK), EV_ROUTE_QOS_COMMAND) == 0);
    return 0;
}
