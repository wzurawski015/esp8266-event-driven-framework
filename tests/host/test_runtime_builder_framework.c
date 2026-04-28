#include <assert.h>

#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_runtime_graph_stats_t stats;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_RTC | EV_CAP_I2C0 | EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_RTC) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_DS18B20) == EV_ERR_NO_CAPABILITY);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
    assert(ev_runtime_graph_get_runtime(&graph, ACT_RTC) != 0);
    assert(ev_runtime_graph_get_runtime(&graph, ACT_FAULT) != 0);
    stats = ev_runtime_graph_stats(&graph);
    assert(stats.actor_count == 3U);
    assert(ev_runtime_graph_pending(&graph) == 0U);
    return 0;
}
