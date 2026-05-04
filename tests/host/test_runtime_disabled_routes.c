#include <assert.h>

#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    const ev_active_route_table_t *routes;
    size_t i;
    unsigned saw_network_disabled = 0U;
    unsigned saw_watchdog_disabled = 0U;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    routes = ev_runtime_graph_active_routes(&graph);
    assert(routes != 0);
    for (i = 0U; i < routes->count; ++i) {
        const ev_active_route_t *entry = ev_active_route_at(routes, i);
        assert(entry != 0);
        if ((entry->route.target_actor == ACT_NETWORK) && (entry->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED)) {
            saw_network_disabled = 1U;
        }
        if ((entry->route.target_actor == ACT_WATCHDOG) && (entry->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED)) {
            saw_watchdog_disabled = 1U;
        }
    }
    assert(saw_network_disabled == 1U);
    assert(saw_watchdog_disabled == 1U);
    assert(graph.metrics.values[EV_METRIC_ROUTE_OPTIONAL_DISABLED] >= 2U);
    return 0;
}
