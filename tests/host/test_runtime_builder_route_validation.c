#include <assert.h>

#include "ev/runtime_graph.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    const ev_active_route_table_t *routes;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    routes = ev_runtime_graph_active_routes(&graph);
    assert(routes != 0);
    assert(routes->count == ev_route_count());
    assert(routes->active_count >= 1U);
    assert(routes->optional_disabled_count > 0U);
    assert(routes->rejected_count == 0U);

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_set_route_validation_flags(&builder, EV_RUNTIME_ROUTE_VALIDATE_STRICT_MANDATORY) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_ERR_NOT_FOUND);
    return 0;
}
