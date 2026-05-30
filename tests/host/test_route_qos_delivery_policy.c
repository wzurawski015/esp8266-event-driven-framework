#include <assert.h>
#include <stddef.h>

#include "ev/metrics_registry.h"
#include "ev/msg.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"

static void assert_policy(ev_route_qos_t qos, ev_delivery_qos_failure_policy_t expected)
{
    assert(ev_delivery_qos_failure_policy(qos) == expected);
    assert(ev_delivery_qos_failure_is_drop(qos) == (expected == EV_DELIVERY_QOS_FAILURE_DROP));
    assert(ev_delivery_qos_failure_is_strict(qos) == (expected == EV_DELIVERY_QOS_FAILURE_STRICT));
}

static ev_route_qos_t route_qos_for(ev_event_id_t event_id, ev_actor_id_t actor_id)
{
    size_t i;

    for (i = 0U; i < ev_route_count(); ++i) {
        const ev_route_t *route = ev_route_at(i);
        if ((route != NULL) && (route->event_id == event_id) && (route->target_actor == actor_id)) {
            return route->qos;
        }
    }

    assert(!"expected route was not declared");
    return EV_ROUTE_QOS_CRITICAL;
}

static void fill_actor_mailbox(ev_runtime_graph_t *graph, ev_actor_id_t actor_id)
{
    size_t i;
    size_t capacity = ev_runtime_graph_actor_mailbox_capacity(graph, actor_id);
    ev_msg_t msg = EV_MSG_INITIALIZER;

    assert(capacity > 0U);
    for (i = 0U; i < capacity; ++i) {
        assert(ev_msg_init_send(&msg, EV_TICK_1S, ACT_APP, actor_id) == EV_OK);
        assert(ev_runtime_graph_send(graph, actor_id, &msg) == EV_OK);
    }
    assert(ev_runtime_graph_pending(graph) >= capacity);
}

static void test_public_failure_policy_table(void)
{
    assert_policy(EV_ROUTE_QOS_CRITICAL, EV_DELIVERY_QOS_FAILURE_STRICT);
    assert_policy(EV_ROUTE_QOS_WAKEUP_CRITICAL, EV_DELIVERY_QOS_FAILURE_STRICT);
    assert_policy(EV_ROUTE_QOS_COMMAND, EV_DELIVERY_QOS_FAILURE_STRICT);

    assert_policy(EV_ROUTE_QOS_BEST_EFFORT, EV_DELIVERY_QOS_FAILURE_DROP);
    assert_policy(EV_ROUTE_QOS_LOSSY, EV_DELIVERY_QOS_FAILURE_DROP);
    assert_policy(EV_ROUTE_QOS_COALESCED, EV_DELIVERY_QOS_FAILURE_DROP);
    assert_policy(EV_ROUTE_QOS_LATEST_ONLY, EV_DELIVERY_QOS_FAILURE_DROP);
    assert_policy(EV_ROUTE_QOS_TELEMETRY, EV_DELIVERY_QOS_FAILURE_DROP);

    assert_policy((ev_route_qos_t)99, EV_DELIVERY_QOS_FAILURE_STRICT);
}

static void test_declared_route_qos_classes(void)
{
    assert(route_qos_for(EV_TIME_UPDATED, ACT_NETWORK) == EV_ROUTE_QOS_TELEMETRY);
    assert(route_qos_for(EV_TEMP_UPDATED, ACT_NETWORK) == EV_ROUTE_QOS_TELEMETRY);
    assert(route_qos_for(EV_SYS_GOTO_SLEEP_CMD, ACT_POWER) == EV_ROUTE_QOS_WAKEUP_CRITICAL);
    assert(route_qos_for(EV_NET_MQTT_MSG_RX, ACT_COMMAND) == EV_ROUTE_QOS_COMMAND);
    assert(route_qos_for(EV_NET_MQTT_MSG_RX_LEASE, ACT_COMMAND) == EV_ROUTE_QOS_COMMAND);
}

static void test_drop_allowed_delivery_failure_reports_ok(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t report;
    ev_capability_mask_t caps = EV_CAP_NET | EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS;

    assert(ev_runtime_builder_init(&builder, &graph, caps, caps) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_APP) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_NETWORK) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    fill_actor_mailbox(&graph, ACT_NETWORK);

    assert(ev_msg_init_publish(&msg, EV_TIME_UPDATED, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_OK);
    assert(report.matched_routes == 2U);
    assert(report.attempted == 2U);
    assert(report.delivered == 1U);
    assert(report.dropped == 1U);
    assert(report.first_error == EV_OK);
    assert(report.first_failed_actor == EV_ACTOR_NONE);
    assert(ev_runtime_graph_metric_value(&graph, EV_METRIC_POST_DROPPED) == 1U);
}

static void test_strict_delivery_failure_reports_error(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t report;
    ev_capability_mask_t caps = EV_CAP_POWER_POLICY | EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS;

    assert(ev_runtime_builder_init(&builder, &graph, caps, caps) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_POWER) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    fill_actor_mailbox(&graph, ACT_POWER);

    assert(ev_msg_init_publish(&msg, EV_SYS_GOTO_SLEEP_CMD, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_ERR_FULL);
    assert(report.matched_routes == 1U);
    assert(report.attempted == 1U);
    assert(report.delivered == 0U);
    assert(report.dropped == 1U);
    assert(report.first_error == EV_ERR_FULL);
    assert(report.first_failed_actor == ACT_POWER);
    assert(ev_runtime_graph_metric_value(&graph, EV_METRIC_MAILBOX_OVERFLOW) == 1U);
}

int main(void)
{
    test_public_failure_policy_table();
    test_declared_route_qos_classes();
    test_drop_allowed_delivery_failure_reports_ok();
    test_strict_delivery_failure_reports_error();
    return 0;
}
