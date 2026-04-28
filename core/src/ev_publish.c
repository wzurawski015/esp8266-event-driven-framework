#include "ev/publish.h"

#include "ev/msg.h"
#include "ev/route_table.h"

void ev_publish_report_reset(ev_publish_report_t *report)
{
    if (report != NULL) {
        report->matched_routes = 0U;
        report->attempted_deliveries = 0U;
        report->delivered_count = 0U;
        report->failed_count = 0U;
        report->first_failed_actor = EV_ACTOR_NONE;
        report->first_error = EV_OK;
    }
}

ev_result_t ev_publish_ex(
    const ev_msg_t *msg,
    ev_delivery_fn_t deliver,
    void *context,
    ev_publish_policy_t policy,
    ev_publish_report_t *report)
{
    ev_result_t rc;
    size_t i;
    ev_route_span_t route_span;
    ev_publish_report_t local_report;

    if ((msg == NULL) || (deliver == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((policy != EV_PUBLISH_FAIL_FAST) && (policy != EV_PUBLISH_BEST_EFFORT)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_publish_report_reset(&local_report);

    rc = ev_msg_validate(msg);
    if (rc != EV_OK) {
        return rc;
    }
    if (msg->target_actor != EV_ACTOR_NONE) {
        return EV_ERR_CONTRACT;
    }

    route_span = ev_route_span_for_event(msg->event_id);
    local_report.matched_routes = route_span.count;
    if (route_span.count == 0U) {
        local_report.first_error = EV_ERR_NOT_FOUND;
        if (report != NULL) {
            *report = local_report;
        }
        return EV_ERR_NOT_FOUND;
    }

    for (i = 0U; i < route_span.count; ++i) {
        const ev_route_t *route = ev_route_at(route_span.start_index + i);
        if (route == NULL) {
            continue;
        }

        ++local_report.attempted_deliveries;
        rc = deliver(route->target_actor, msg, context);
        if (rc == EV_OK) {
            ++local_report.delivered_count;
            continue;
        }

        ++local_report.failed_count;
        if (local_report.first_error == EV_OK) {
            local_report.first_error = rc;
            local_report.first_failed_actor = route->target_actor;
        }

        if (policy == EV_PUBLISH_FAIL_FAST) {
            if (report != NULL) {
                *report = local_report;
            }
            return rc;
        }
    }

    if (report != NULL) {
        *report = local_report;
    }

    if (local_report.failed_count == 0U) {
        return EV_OK;
    }
    if (local_report.delivered_count > 0U) {
        return EV_ERR_PARTIAL;
    }

    return local_report.first_error;
}

ev_result_t ev_publish(
    const ev_msg_t *msg,
    ev_delivery_fn_t deliver,
    void *context,
    size_t *delivered_count)
{
    ev_publish_report_t report;
    ev_result_t rc;

    ev_publish_report_reset(&report);
    rc = ev_publish_ex(msg, deliver, context, EV_PUBLISH_FAIL_FAST, &report);
    if (delivered_count != NULL) {
        *delivered_count = report.delivered_count;
    }
    return rc;
}
