#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_result_t rc = ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS);
    if (rc != EV_OK) {
        return 1;
    }
    if (ev_runtime_builder_add_module(&builder, ACT_FAULT) != EV_OK) {
        return 1;
    }
    if (ev_runtime_builder_add_module(&builder, ACT_METRICS) != EV_OK) {
        return 1;
    }
    return (ev_runtime_builder_build(&builder) == EV_OK) ? 0 : 1;
}
