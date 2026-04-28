#include <assert.h>

#include "ev/command_security.h"
#include "ev/delivery_service.h"
#include "ev/network_outbox.h"
#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_delivery_service_t delivery;
    ev_delivery_report_t report;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_command_auth_port_t auth = {"secret"};
    ev_command_authorizer_t authorizer = {EV_COMMAND_CAP_LED};
    ev_command_policy_t command_policy = {EV_COMMAND_CAP_LED, 0U, 0U, 0U};
    ev_command_audit_record_t audit = {0U, 0U, 0U, 0U, 0U, 0U};
    ev_network_outbox_t outbox;
    ev_network_backpressure_policy_t bp = {{1U, 1U, 1U, 1U}, {1U, 0U, 0U, 0U}};
    const uint8_t payload[2] = {1U, 2U};

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    ev_delivery_service_init(&delivery, &graph);
    assert(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP) == EV_OK);
    assert(ev_delivery_publish(&delivery, &msg, &report) == EV_OK);
    assert(report.matched_routes == 1U);
    assert(report.delivered == 1U);
    assert(ev_runtime_graph_pending(&graph) == 1U);

    assert(ev_command_authenticate(&auth, "bad", &audit) == EV_ERR_AUTH);
    assert(ev_command_authenticate(&auth, "secret", &audit) == EV_OK);
    assert(ev_command_authorize(&authorizer, &command_policy, 100U, &audit) == EV_OK);
    assert(audit.auth_failed == 1U);
    assert(audit.accepted == 1U);

    ev_network_outbox_init(&outbox);
    assert(ev_network_outbox_push(&outbox, &bp, EV_NETWORK_MSG_TELEMETRY_LATEST, payload, sizeof(payload)) == EV_OK);
    assert(ev_network_outbox_push(&outbox, &bp, EV_NETWORK_MSG_TELEMETRY_LATEST, payload, sizeof(payload)) == EV_OK);
    assert(outbox.dropped == 1U);
    assert(ev_network_outbox_push(&outbox, &bp, EV_NETWORK_MSG_COMMAND_RESPONSE, payload, sizeof(payload)) == EV_OK);
    assert(ev_network_outbox_push(&outbox, &bp, EV_NETWORK_MSG_COMMAND_RESPONSE, payload, sizeof(payload)) == EV_ERR_FULL);
    return 0;
}
