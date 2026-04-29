#include <assert.h>

#include "ev/runtime_graph.h"
#include "ev/runtime_poll.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_runtime_poll_report_t report;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
    assert(ev_system_pump_bound_count(&graph.scheduler.system) > 0U);

    assert(ev_msg_init_send(&msg, EV_COMMAND_ACCEPTED, ACT_APP, ACT_METRICS) == EV_OK);
    assert(ev_runtime_graph_send(&graph, ACT_METRICS, &msg) == EV_OK);
    assert(ev_runtime_poll_once(&graph, 0U, 1U, &report) == EV_OK);
    assert(report.domains_pumped > 0U);
    assert(report.messages_processed == 1U);
    assert(graph.scheduler.poll_calls == 1U);
    return 0;
}
