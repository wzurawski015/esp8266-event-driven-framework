#include <assert.h>
#include <stddef.h>

#include "ev/active_route_table.h"
#include "ev/actor_catalog.h"
#include "ev/actor_publish_port.h"
#include "ev/runtime_graph.h"

static ev_route_t make_route(ev_event_id_t event_id, ev_actor_id_t target_actor)
{
    ev_route_t route;
    route.event_id = event_id;
    route.target_actor = target_actor;
    route.qos = EV_ROUTE_QOS_CRITICAL;
    route.priority = 0U;
    route.flags = 0U;
    return route;
}

static void assert_empty_table_spans_are_safe(void)
{
    ev_active_route_table_t table;
    ev_route_span_t span;

    ev_active_route_table_init(&table);
    assert(ev_active_route_table_finalize_spans(&table) == EV_OK);

    span = ev_active_route_table_span_for_event(&table, EV_BOOT_STARTED);
    assert(span.start_index == 0U);
    assert(span.count == 0U);

    span = ev_active_route_table_span_for_event(&table, (ev_event_id_t)EV_EVENT_COUNT);
    assert(span.start_index == 0U);
    assert(span.count == 0U);
}

static void assert_direct_table_spans_are_contiguous_and_deterministic(void)
{
    ev_active_route_table_t table;
    ev_route_t route;
    ev_route_span_t tick_span;
    ev_route_span_t ready_span;
    ev_route_span_t absent_span;

    ev_active_route_table_init(&table);

    route = make_route(EV_TICK_1S, ACT_DIAG);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_ENABLED, EV_OK) == EV_OK);
    route = make_route(EV_TICK_1S, ACT_APP);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_ENABLED, EV_OK) == EV_OK);
    route = make_route(EV_SYSTEM_READY, ACT_APP);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_OPTIONAL_DISABLED, EV_ERR_NO_CAPABILITY) == EV_OK);

    tick_span = ev_active_route_table_span_for_event(&table, EV_TICK_1S);
    assert(tick_span.start_index == 0U);
    assert(tick_span.count == 0U);

    assert(ev_active_route_table_finalize_spans(&table) == EV_OK);

    tick_span = ev_active_route_table_span_for_event(&table, EV_TICK_1S);
    assert(tick_span.start_index == 0U);
    assert(tick_span.count == 2U);
    assert(ev_active_route_at(&table, tick_span.start_index)->route.target_actor == ACT_DIAG);
    assert(ev_active_route_at(&table, tick_span.start_index + 1U)->route.target_actor == ACT_APP);

    ready_span = ev_active_route_table_span_for_event(&table, EV_SYSTEM_READY);
    assert(ready_span.start_index == 2U);
    assert(ready_span.count == 1U);
    assert(ev_active_route_at(&table, ready_span.start_index)->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED);

    absent_span = ev_active_route_table_span_for_event(&table, EV_BOOT_STARTED);
    assert(absent_span.start_index == 0U);
    assert(absent_span.count == 0U);

    assert(ev_active_route_table_finalize_spans(&table) == EV_OK);
    assert(ev_active_route_table_span_for_event(&table, EV_TICK_1S).start_index == tick_span.start_index);
    assert(ev_active_route_table_span_for_event(&table, EV_TICK_1S).count == tick_span.count);
    assert(ev_active_route_table_span_for_event(&table, EV_SYSTEM_READY).start_index == ready_span.start_index);
    assert(ev_active_route_table_span_for_event(&table, EV_SYSTEM_READY).count == ready_span.count);
}

static void assert_non_contiguous_event_routes_are_rejected(void)
{
    ev_active_route_table_t table;
    ev_route_t route;
    ev_route_span_t span;

    ev_active_route_table_init(&table);

    route = make_route(EV_TICK_1S, ACT_DIAG);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_ENABLED, EV_OK) == EV_OK);
    route = make_route(EV_SYSTEM_READY, ACT_APP);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_ENABLED, EV_OK) == EV_OK);
    route = make_route(EV_TICK_1S, ACT_WATCHDOG);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_ENABLED, EV_OK) == EV_OK);

    assert(ev_active_route_table_finalize_spans(&table) == EV_ERR_CONTRACT);
    span = ev_active_route_table_span_for_event(&table, EV_TICK_1S);
    assert(span.start_index == 0U);
    assert(span.count == 0U);
}

static void assert_invalid_stored_event_is_rejected(void)
{
    ev_active_route_table_t table;
    ev_route_t route;

    ev_active_route_table_init(&table);
    route = make_route((ev_event_id_t)EV_EVENT_COUNT, ACT_APP);
    assert(ev_active_route_table_add(&table, &route, EV_ACTIVE_ROUTE_REJECTED_INVALID_EVENT, EV_ERR_OUT_OF_RANGE) == EV_OK);
    assert(ev_active_route_table_finalize_spans(&table) == EV_ERR_OUT_OF_RANGE);
}

static void assert_builder_finalizes_active_route_spans(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    const ev_active_route_table_t *routes;
    ev_route_span_t fault_span;
    ev_route_span_t tick_span;
    ev_route_span_t static_tick_span;
    ev_delivery_report_t report;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    assert(ev_runtime_builder_init(&builder,
                                   &graph,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    routes = ev_runtime_graph_active_routes(&graph);
    assert(routes != NULL);
    assert(routes->count == ev_route_count());

    fault_span = ev_active_route_table_span_for_event(routes, EV_FAULT_REPORTED);
    assert(fault_span.count == 1U);
    assert(ev_active_route_at(routes, fault_span.start_index)->route.target_actor == ACT_FAULT);
    assert(ev_active_route_at(routes, fault_span.start_index)->state == EV_ACTIVE_ROUTE_ENABLED);

    static_tick_span = ev_route_span_for_event(EV_TICK_1S);
    tick_span = ev_active_route_table_span_for_event(routes, EV_TICK_1S);
    assert(tick_span.count == static_tick_span.count);
    assert(tick_span.count == 9U);

    assert(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_OK);
    assert(report.matched_routes == fault_span.count);
    assert(report.delivered == 1U);

    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(&graph, &msg, &report) == EV_OK);
    assert(report.matched_routes == tick_span.count);
    assert(report.attempted == 0U);
    assert(report.delivered == 0U);
    assert(report.optional_disabled_routes == tick_span.count);
    assert(report.dropped == tick_span.count);
}

static void assert_actor_send_route_lookup_uses_active_span_semantics(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_actor_publish_port_t port;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    assert(ev_runtime_builder_init(&builder,
                                   &graph,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
    assert(ev_actor_publish_port_init(&port, &graph, ACT_APP) == EV_OK);

    assert(ev_msg_init_send(&msg, EV_TICK_1S, ACT_APP, ACT_NETWORK) == EV_OK);
    assert(ev_actor_send(&port, ACT_NETWORK, &msg) == EV_OK);
    assert(port.stats.sends_attempted == 1U);
    assert(port.stats.sends_ok == 0U);
    assert(port.stats.optional_disabled_routes == 1U);
    assert(port.stats.optional_disabled_network_routes == 1U);
}

int main(void)
{
    assert_empty_table_spans_are_safe();
    assert_direct_table_spans_are_contiguous_and_deterministic();
    assert_non_contiguous_event_routes_are_rejected();
    assert_invalid_stored_event_is_rejected();
    assert_builder_finalizes_active_route_spans();
    assert_actor_send_route_lookup_uses_active_span_semantics();
    return 0;
}
