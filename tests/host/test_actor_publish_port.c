#include <assert.h>

#include "ev/actor_publish_port.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_actor_publish_port_t port;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    assert(ev_actor_publish_port_init(&port, &graph, ACT_APP) == EV_OK);
    assert(ev_msg_init_send(&msg, EV_COMMAND_ACCEPTED, ACT_APP, ACT_METRICS) == EV_OK);
    assert(ev_actor_publish_port_delivery_adapter(ACT_METRICS, &msg, &port) == EV_OK);
    assert(port.stats.sends_attempted == 1U);
    assert(port.stats.sends_ok == 1U);

    assert(ev_msg_init_send(&msg, EV_TICK_1S, ACT_APP, ACT_NETWORK) == EV_OK);
    assert(ev_actor_send(&port, ACT_NETWORK, &msg) == EV_OK);
    assert(port.stats.optional_disabled_routes == 1U);
    assert(graph.metrics.values[EV_METRIC_ROUTE_DISABLED_SKIPPED] > 0U);
    return 0;
}
