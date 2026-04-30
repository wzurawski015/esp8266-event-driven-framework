#include "ev/actor_publish_port.h"

#include <string.h>

#include "ev/active_route_table.h"
#include "ev/metrics_registry.h"

static const ev_active_route_t *ev_actor_publish_find_route(ev_runtime_graph_t *graph,
                                                            ev_event_id_t event_id,
                                                            ev_actor_id_t target_actor)
{
    const ev_active_route_table_t *routes;
    size_t i;

    if (graph == NULL) {
        return NULL;
    }
    routes = ev_runtime_graph_active_routes(graph);
    if (routes == NULL) {
        return NULL;
    }
    for (i = 0U; i < routes->count; ++i) {
        const ev_active_route_t *entry = ev_active_route_at(routes, i);
        if ((entry != NULL) &&
            (entry->route.event_id == event_id) &&
            (entry->route.target_actor == target_actor)) {
            return entry;
        }
    }
    return NULL;
}

ev_result_t ev_actor_publish_port_init(ev_actor_publish_port_t *port,
                                       ev_runtime_graph_t *graph,
                                       ev_actor_id_t source_actor)
{
    if ((port == NULL) || (graph == NULL) || !ev_actor_id_is_valid(source_actor)) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(port, 0, sizeof(*port));
    port->graph = graph;
    port->source_actor = source_actor;
    return EV_OK;
}

ev_result_t ev_actor_publish(ev_actor_publish_port_t *port, const ev_msg_t *msg, ev_delivery_report_t *out_report)
{
    ev_result_t rc;
    if ((port == NULL) || (port->graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_runtime_graph_publish(port->graph, msg, out_report);
    return rc;
}

ev_result_t ev_actor_send(ev_actor_publish_port_t *port, ev_actor_id_t target_actor, const ev_msg_t *msg)
{
    const ev_active_route_t *route;
    ev_result_t rc;

    if ((port == NULL) || (port->graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(target_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    port->stats.sends_attempted++;
    route = ev_actor_publish_find_route(port->graph, msg->event_id, target_actor);
    if (route != NULL) {
        if (route->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
            port->stats.optional_disabled_routes++;
            if (target_actor == ACT_WATCHDOG) {
                port->stats.optional_disabled_watchdog_routes++;
            }
            if (target_actor == ACT_NETWORK) {
                port->stats.optional_disabled_network_routes++;
            }
            (void)ev_metric_increment(&port->graph->metrics, EV_METRIC_ROUTE_DISABLED_SKIPPED, 1U);
            return EV_OK;
        }
        if (route->state != EV_ACTIVE_ROUTE_ENABLED) {
            port->stats.failed++;
            return (route->reason != EV_OK) ? route->reason : EV_ERR_STATE;
        }
    }
    rc = ev_runtime_graph_send(port->graph, target_actor, msg);
    if (rc == EV_OK) {
        port->stats.sends_ok++;
    } else {
        port->stats.failed++;
    }
    return rc;
}

ev_result_t ev_actor_publish_port_delivery_adapter(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    return ev_actor_send((ev_actor_publish_port_t *)context, target_actor, msg);
}

const ev_actor_publish_port_stats_t *ev_actor_publish_port_stats(const ev_actor_publish_port_t *port)
{
    return (port != NULL) ? &port->stats : NULL;
}
