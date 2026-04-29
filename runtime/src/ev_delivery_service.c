#include "ev/delivery_service.h"

#include <string.h>

#include "ev/active_route_table.h"
#include "ev/fault_bus.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_graph.h"
#include "ev/trace_ring.h"

void ev_delivery_service_init(ev_delivery_service_t *svc, ev_runtime_graph_t *graph)
{
    if (svc != NULL) {
        svc->graph = graph;
    }
}

void ev_delivery_report_reset(ev_delivery_report_t *report)
{
    if (report != NULL) {
        (void)memset(report, 0, sizeof(*report));
        report->first_error = EV_OK;
        report->first_failed_actor = EV_ACTOR_NONE;
    }
}

static void ev_delivery_emit_fault(ev_runtime_graph_t *graph, ev_actor_id_t source_actor, ev_actor_id_t target_actor, ev_event_id_t event_id, ev_result_t result)
{
    ev_fault_payload_t fault;
    if (graph == NULL) {
        return;
    }
    (void)memset(&fault, 0, sizeof(fault));
    fault.fault_id = EV_FAULT_MAILBOX_OVERFLOW;
    fault.severity = EV_FAULT_SEV_ERROR;
    fault.source_actor = source_actor;
    fault.triggering_event = event_id;
    fault.arg0 = (uint32_t)target_actor;
    fault.arg1 = (uint32_t)(-result);
    (void)ev_fault_emit(&graph->faults, &fault);
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_FAULT_EMITTED, 1U);
}

static void ev_delivery_trace(ev_runtime_graph_t *graph, const ev_route_t *route, const ev_msg_t *msg, ev_result_t result)
{
    ev_trace_record_t rec;
    if ((graph == NULL) || (route == NULL) || (msg == NULL)) {
        return;
    }
    rec.timestamp_us = 0U;
    rec.event_id = msg->event_id;
    rec.source_actor = msg->source_actor;
    rec.target_actor = route->target_actor;
    rec.result = result;
    rec.qos = route->qos;
    rec.queue_depth = 0U;
    rec.flags = route->flags;
    if (ev_trace_record(&graph->trace_ring, &rec) != EV_OK) {
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_TRACE_DROPPED, 1U);
    }
}

static int ev_delivery_route_failure_is_drop(ev_route_qos_t qos)
{
    return (qos == EV_ROUTE_QOS_LOSSY) ||
           (qos == EV_ROUTE_QOS_BEST_EFFORT) ||
           (qos == EV_ROUTE_QOS_TELEMETRY) ||
           (qos == EV_ROUTE_QOS_LATEST_ONLY) ||
           (qos == EV_ROUTE_QOS_COALESCED);
}

static ev_result_t ev_delivery_deliver_one(ev_runtime_graph_t *graph, const ev_route_t *route, const ev_msg_t *msg, ev_delivery_report_t *local)
{
    ev_msg_t send_msg;
    ev_result_t rc;

    send_msg = *msg;
    send_msg.target_actor = route->target_actor;
    local->attempted++;
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_DELIVERY_ATTEMPTED, 1U);
    rc = ev_actor_registry_delivery(route->target_actor, &send_msg, &graph->registry);
    ev_delivery_trace(graph, route, &send_msg, rc);

    if (rc == EV_OK) {
        local->delivered++;
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_DELIVERY_OK, 1U);
        return EV_OK;
    }

    local->dropped++;
    (void)ev_metric_increment(&graph->metrics, EV_METRIC_DELIVERY_FAILED, 1U);
    if (ev_delivery_route_failure_is_drop(route->qos) != 0) {
        (void)ev_metric_increment(&graph->metrics, EV_METRIC_POST_DROPPED, 1U);
        return EV_OK;
    }
    if (local->first_error == EV_OK) {
        local->first_error = rc;
        local->first_failed_actor = route->target_actor;
    }
    ev_delivery_emit_fault(graph, msg->source_actor, route->target_actor, msg->event_id, rc);
    return rc;
}

static ev_result_t ev_delivery_publish_active(ev_runtime_graph_t *graph, const ev_msg_t *msg, ev_delivery_report_t *local)
{
    size_t i;
    ev_result_t final_rc = EV_OK;

    for (i = 0U; i < graph->active_routes.count; ++i) {
        const ev_active_route_t *entry = ev_active_route_at(&graph->active_routes, i);
        ev_result_t rc;
        if ((entry == NULL) || (entry->route.event_id != msg->event_id)) {
            continue;
        }
        local->matched_routes++;
        if (entry->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
            local->dropped++;
            local->optional_disabled_routes++;
            if (entry->route.target_actor == ACT_WATCHDOG) {
                local->optional_disabled_watchdog_routes++;
            }
            if (entry->route.target_actor == ACT_NETWORK) {
                local->optional_disabled_network_routes++;
            }
            (void)ev_metric_increment(&graph->metrics, EV_METRIC_ROUTE_DISABLED_SKIPPED, 1U);
            continue;
        }
        if (entry->state != EV_ACTIVE_ROUTE_ENABLED) {
            local->dropped++;
            (void)ev_metric_increment(&graph->metrics, EV_METRIC_ROUTE_VALIDATION_REJECTED, 1U);
            if (local->first_error == EV_OK) {
                local->first_error = entry->reason;
                local->first_failed_actor = entry->route.target_actor;
            }
            final_rc = entry->reason;
            continue;
        }
        rc = ev_delivery_deliver_one(graph, &entry->route, msg, local);
        if (rc != EV_OK) {
            final_rc = rc;
            if (entry->route.qos == EV_ROUTE_QOS_CRITICAL || entry->route.qos == EV_ROUTE_QOS_WAKEUP_CRITICAL || entry->route.qos == EV_ROUTE_QOS_COMMAND) {
                break;
            }
        }
    }
    return final_rc;
}

static ev_result_t ev_delivery_publish_static(ev_runtime_graph_t *graph, const ev_msg_t *msg, ev_delivery_report_t *local)
{
    ev_route_span_t span = ev_route_span_for_event(msg->event_id);
    size_t i;
    ev_result_t final_rc = EV_OK;

    local->matched_routes = span.count;
    for (i = 0U; i < span.count; ++i) {
        const ev_route_t *route = ev_route_at(span.start_index + i);
        ev_result_t rc;
        if (route == NULL) {
            continue;
        }
        rc = ev_delivery_deliver_one(graph, route, msg, local);
        if (rc != EV_OK) {
            final_rc = rc;
            if (route->qos == EV_ROUTE_QOS_CRITICAL || route->qos == EV_ROUTE_QOS_WAKEUP_CRITICAL || route->qos == EV_ROUTE_QOS_COMMAND) {
                break;
            }
        }
    }
    return final_rc;
}

ev_result_t ev_delivery_publish(ev_delivery_service_t *svc, const ev_msg_t *msg, ev_delivery_report_t *report)
{
    ev_delivery_report_t local;
    ev_result_t final_rc;

    if ((svc == NULL) || (svc->graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_delivery_report_reset(&local);
    if (svc->graph->active_routes_bound != 0U) {
        final_rc = ev_delivery_publish_active(svc->graph, msg, &local);
    } else {
        final_rc = ev_delivery_publish_static(svc->graph, msg, &local);
    }

    if (local.matched_routes == 0U) {
        local.first_error = EV_ERR_NOT_FOUND;
        final_rc = EV_ERR_NOT_FOUND;
    }
    if (report != NULL) {
        *report = local;
    }
    return final_rc;
}
