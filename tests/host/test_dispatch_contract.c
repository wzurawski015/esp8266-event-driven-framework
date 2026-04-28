#include <assert.h>
#include <stddef.h>

#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/send.h"

typedef struct {
    size_t call_count;
    ev_actor_id_t call_targets[8];
    ev_event_id_t last_event;
    ev_actor_id_t fail_target;
    ev_result_t fail_result;
} trace_t;

static ev_result_t trace_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    trace_t *trace = (trace_t *)context;

    trace->last_event = msg->event_id;
    if (trace->call_count < (sizeof(trace->call_targets) / sizeof(trace->call_targets[0]))) {
        trace->call_targets[trace->call_count] = target_actor;
    }
    ++trace->call_count;

    if ((trace->fail_result != EV_OK) && (target_actor == trace->fail_target)) {
        return trace->fail_result;
    }

    return EV_OK;
}

int main(void)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    trace_t trace = {0};
    ev_publish_report_t report;
    size_t delivered = 0U;

    assert(ev_msg_init_publish(&msg, EV_BOOT_STARTED, ACT_BOOT) == EV_OK);
    assert(ev_publish(&msg, trace_delivery, &trace, &delivered) == EV_OK);
    assert(delivered == 1U);
    assert(trace.call_count == 1U);
    assert(trace.call_targets[0] == ACT_DIAG);
    assert(trace.last_event == EV_BOOT_STARTED);

    trace = (trace_t){0};
    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    assert(ev_send(ACT_DIAG, &msg, trace_delivery, &trace) == EV_OK);
    assert(trace.call_count == 1U);
    assert(trace.call_targets[0] == ACT_DIAG);
    assert(trace.last_event == EV_DIAG_SNAPSHOT_REQ);

    trace = (trace_t){0};
    assert(ev_send(ACT_APP, &msg, trace_delivery, &trace) == EV_ERR_CONTRACT);
    assert(trace.call_count == 0U);

    trace = (trace_t){0};
    assert(ev_publish(&msg, trace_delivery, &trace, &delivered) == EV_ERR_CONTRACT);
    assert(trace.call_count == 0U);

    trace = (trace_t){0};
    trace.fail_target = ACT_APP;
    trace.fail_result = EV_ERR_FULL;
    ev_publish_report_reset(&report);
    assert(ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT) == EV_OK);
    assert(ev_publish_ex(&msg, trace_delivery, &trace, EV_PUBLISH_FAIL_FAST, &report) == EV_ERR_FULL);
    assert(report.matched_routes == 7U);
    assert(report.attempted_deliveries == 2U);
    assert(report.delivered_count == 1U);
    assert(report.failed_count == 1U);
    assert(report.first_failed_actor == ACT_APP);
    assert(report.first_error == EV_ERR_FULL);
    assert(trace.call_count == 2U);
    assert(trace.call_targets[0] == ACT_DIAG);
    assert(trace.call_targets[1] == ACT_APP);

    trace = (trace_t){0};
    trace.fail_target = ACT_APP;
    trace.fail_result = EV_ERR_FULL;
    ev_publish_report_reset(&report);
    assert(ev_publish_ex(&msg, trace_delivery, &trace, EV_PUBLISH_BEST_EFFORT, &report) == EV_ERR_PARTIAL);
    assert(report.matched_routes == 7U);
    assert(report.attempted_deliveries == 7U);
    assert(report.delivered_count == 6U);
    assert(report.failed_count == 1U);
    assert(report.first_failed_actor == ACT_APP);
    assert(report.first_error == EV_ERR_FULL);
    assert(trace.call_count == 7U);

    trace = (trace_t){0};
    trace.fail_target = ACT_DIAG;
    trace.fail_result = EV_ERR_NOT_FOUND;
    ev_publish_report_reset(&report);
    assert(ev_publish_ex(&msg, trace_delivery, &trace, EV_PUBLISH_FAIL_FAST, &report) == EV_ERR_NOT_FOUND);
    assert(report.matched_routes == 7U);
    assert(report.attempted_deliveries == 1U);
    assert(report.delivered_count == 0U);
    assert(report.failed_count == 1U);
    assert(report.first_failed_actor == ACT_DIAG);
    assert(report.first_error == EV_ERR_NOT_FOUND);
    assert(trace.call_count == 1U);

    return 0;
}
