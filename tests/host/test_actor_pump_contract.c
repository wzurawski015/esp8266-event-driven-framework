#include <assert.h>
#include <stddef.h>

#include "ev/actor_catalog.h"
#include "ev/actor_runtime.h"
#include "ev/msg.h"
#include "ev/send.h"

typedef struct {
    size_t calls;
    size_t fail_on_call;
    ev_result_t fail_result;
    ev_event_id_t last_event;
} pump_trace_t;

static ev_result_t pump_handler(void *actor_context, const ev_msg_t *msg)
{
    pump_trace_t *trace = (pump_trace_t *)actor_context;
    ++trace->calls;
    trace->last_event = msg->event_id;
    if ((trace->fail_on_call != 0U) && (trace->calls == trace->fail_on_call)) {
        return trace->fail_result;
    }
    return EV_OK;
}

static void enqueue_diag_requests(ev_mailbox_t *mailbox, size_t count)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t i;

    assert(mailbox != NULL);
    for (i = 0U; i < count; ++i) {
        assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
        assert(ev_mailbox_push(mailbox, &msg) == EV_OK);
    }
}

int main(void)
{
    ev_msg_t diag_storage[8] = {{0}};
    ev_msg_t diag_storage_fail[8] = {{0}};
    ev_mailbox_t diag_mailbox;
    ev_mailbox_t diag_mailbox_fail;
    ev_actor_runtime_t diag_runtime;
    ev_actor_runtime_t diag_runtime_fail;
    pump_trace_t trace = {0};
    pump_trace_t fail_trace = {0};
    ev_actor_pump_report_t report;
    const ev_actor_runtime_stats_t *stats;

    assert(ev_actor_default_drain_budget(ACT_DIAG) == 2U);
    assert(ev_mailbox_init(&diag_mailbox, EV_MAILBOX_FIFO_8, diag_storage, 8U) == EV_OK);
    assert(ev_actor_runtime_init(&diag_runtime, ACT_DIAG, &diag_mailbox, pump_handler, &trace) == EV_OK);
    assert(ev_actor_runtime_default_budget(&diag_runtime) == 2U);

    enqueue_diag_requests(&diag_mailbox, 3U);
    ev_actor_pump_report_reset(&report);
    assert(ev_actor_runtime_pump_default(&diag_runtime, &report) == EV_OK);
    assert(report.budget == 2U);
    assert(report.pending_before == 3U);
    assert(report.processed == 2U);
    assert(report.pending_after == 1U);
    assert(report.exhausted_budget);
    assert(report.stop_result == EV_OK);
    assert(trace.calls == 2U);
    assert(trace.last_event == EV_DIAG_SNAPSHOT_REQ);

    stats = ev_actor_runtime_stats(&diag_runtime);
    assert(stats != NULL);
    assert(stats->pump_calls == 1U);
    assert(stats->pump_budget_hits == 1U);
    assert(stats->last_pump_budget == 2U);
    assert(stats->last_pump_processed == 2U);
    assert(stats->steps_ok == 2U);
    assert(stats->steps_empty == 0U);

    ev_actor_pump_report_reset(&report);
    assert(ev_actor_runtime_pump_default(&diag_runtime, &report) == EV_OK);
    assert(report.budget == 2U);
    assert(report.pending_before == 1U);
    assert(report.processed == 1U);
    assert(report.pending_after == 0U);
    assert(!report.exhausted_budget);
    assert(report.stop_result == EV_ERR_EMPTY);
    assert(trace.calls == 3U);

    stats = ev_actor_runtime_stats(&diag_runtime);
    assert(stats->pump_calls == 2U);
    assert(stats->pump_budget_hits == 1U);
    assert(stats->last_pump_processed == 1U);
    assert(stats->steps_ok == 3U);
    assert(stats->steps_empty == 0U);

    ev_actor_pump_report_reset(&report);
    assert(ev_actor_runtime_pump_default(&diag_runtime, &report) == EV_ERR_EMPTY);
    assert(report.budget == 2U);
    assert(report.pending_before == 0U);
    assert(report.processed == 0U);
    assert(report.pending_after == 0U);
    assert(!report.exhausted_budget);
    assert(report.stop_result == EV_ERR_EMPTY);

    stats = ev_actor_runtime_stats(&diag_runtime);
    assert(stats->pump_calls == 3U);
    assert(stats->steps_empty == 1U);
    assert(stats->last_pump_budget == 2U);
    assert(stats->last_pump_processed == 0U);

    enqueue_diag_requests(&diag_mailbox, 2U);
    ev_actor_pump_report_reset(&report);
    assert(ev_actor_runtime_pump(&diag_runtime, 1U, &report) == EV_OK);
    assert(report.budget == 1U);
    assert(report.pending_before == 2U);
    assert(report.processed == 1U);
    assert(report.pending_after == 1U);
    assert(report.exhausted_budget);
    assert(report.stop_result == EV_OK);

    stats = ev_actor_runtime_stats(&diag_runtime);
    assert(stats->pump_calls == 4U);
    assert(stats->pump_budget_hits == 2U);
    assert(stats->last_pump_budget == 1U);
    assert(stats->last_pump_processed == 1U);

    assert(ev_mailbox_init(&diag_mailbox_fail, EV_MAILBOX_FIFO_8, diag_storage_fail, 8U) == EV_OK);
    fail_trace.fail_on_call = 2U;
    fail_trace.fail_result = EV_ERR_STATE;
    assert(ev_actor_runtime_init(&diag_runtime_fail, ACT_DIAG, &diag_mailbox_fail, pump_handler, &fail_trace) == EV_OK);
    enqueue_diag_requests(&diag_mailbox_fail, 3U);

    ev_actor_pump_report_reset(&report);
    assert(ev_actor_runtime_pump(&diag_runtime_fail, 4U, &report) == EV_ERR_STATE);
    assert(report.budget == 4U);
    assert(report.pending_before == 3U);
    assert(report.processed == 2U);
    assert(report.pending_after == 1U);
    assert(!report.exhausted_budget);
    assert(report.stop_result == EV_ERR_STATE);
    assert(fail_trace.calls == 2U);

    stats = ev_actor_runtime_stats(&diag_runtime_fail);
    assert(stats->pump_calls == 1U);
    assert(stats->pump_budget_hits == 0U);
    assert(stats->last_pump_budget == 4U);
    assert(stats->last_pump_processed == 2U);
    assert(stats->steps_ok == 1U);
    assert(stats->handler_errors == 1U);
    assert(stats->last_result == EV_ERR_STATE);
    assert(ev_actor_runtime_pending(&diag_runtime_fail) == 1U);

    assert(ev_actor_runtime_pump(&diag_runtime, 0U, &report) == EV_ERR_INVALID_ARG);

    return 0;
}
