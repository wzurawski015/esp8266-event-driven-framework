#include <assert.h>

#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t report;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    assert(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_OK);
    assert(report.matched_routes == 1U);
    assert(report.delivered == 1U);
    assert(ev_runtime_graph_pending(&graph) == 1U);

    assert(ev_msg_init_send(&msg, EV_TICK_1S, ACT_APP, ACT_METRICS) == EV_OK);
    assert(ev_runtime_graph_send(&graph, ACT_METRICS, &msg) == EV_OK);
    assert(ev_runtime_graph_pending(&graph) == 2U);

    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_OK);
    assert(report.matched_routes > 0U);
    assert(report.delivered == 0U);
    assert(report.dropped > 0U);
    assert(graph.metrics.values[EV_METRIC_ROUTE_DISABLED_SKIPPED] > 0U);
    assert(graph.metrics.values[EV_METRIC_POST_OK] > 0U);
    return 0;
}
