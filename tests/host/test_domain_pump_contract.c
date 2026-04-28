#include <assert.h>
#include <stddef.h>

#include "ev/actor_runtime.h"
#include "ev/domain_pump.h"
#include "ev/mailbox.h"
#include "ev/msg.h"

typedef struct {
    size_t calls;
    size_t fail_on_call;
    ev_result_t fail_result;
    ev_event_id_t last_event;
} domain_trace_t;

static ev_result_t domain_handler(void *actor_context, const ev_msg_t *msg)
{
    domain_trace_t *trace = (domain_trace_t *)actor_context;
    ++trace->calls;
    trace->last_event = msg->event_id;
    if ((trace->fail_on_call != 0U) && (trace->calls == trace->fail_on_call)) {
        return trace->fail_result;
    }
    return EV_OK;
}

static void enqueue_boot(ev_mailbox_t *mailbox, size_t count)
{
    ev_msg_t msg = {0};
    size_t i;

    for (i = 0U; i < count; ++i) {
        assert(ev_msg_init_send(&msg, EV_BOOT_COMPLETED, ACT_BOOT, ACT_BOOT) == EV_OK);
        assert(ev_mailbox_push(mailbox, &msg) == EV_OK);
    }
}

static void enqueue_diag(ev_mailbox_t *mailbox, size_t count)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t i;

    for (i = 0U; i < count; ++i) {
        assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
        assert(ev_mailbox_push(mailbox, &msg) == EV_OK);
    }
}

static void enqueue_stream(ev_mailbox_t *mailbox, size_t count)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t i;

    for (i = 0U; i < count; ++i) {
        assert(ev_msg_init_send(&msg, EV_STREAM_CHUNK_READY, ACT_APP, ACT_STREAM) == EV_OK);
        assert(ev_mailbox_push(mailbox, &msg) == EV_OK);
    }
}

static void enqueue_app(ev_mailbox_t *mailbox, size_t count)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t i;

    for (i = 0U; i < count; ++i) {
        assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_BOOT, ACT_APP) == EV_OK);
        assert(ev_mailbox_push(mailbox, &msg) == EV_OK);
    }
}

/* cursor-stability regression: one pass with budget > 1 must not re-base after first actor */
static void domain_pump_cursor_regression(void)
{
    ev_msg_t boot_storage[8] = {{0}};
    ev_msg_t stream_storage[16] = {{0}};
    ev_msg_t app_storage[8] = {{0}};
    ev_mailbox_t boot_mailbox;
    ev_mailbox_t stream_mailbox;
    ev_mailbox_t app_mailbox;
    ev_actor_runtime_t boot_runtime;
    ev_actor_runtime_t stream_runtime;
    ev_actor_runtime_t app_runtime;
    ev_actor_registry_t registry = {0};
    ev_domain_pump_t fast_pump;
    ev_domain_pump_report_t report;
    domain_trace_t boot_trace = {0};
    domain_trace_t stream_trace = {0};
    domain_trace_t app_trace = {0};

    assert(ev_mailbox_init(&boot_mailbox, EV_MAILBOX_FIFO_8, boot_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&stream_mailbox, EV_MAILBOX_FIFO_16, stream_storage, 16U) == EV_OK);
    assert(ev_mailbox_init(&app_mailbox, EV_MAILBOX_FIFO_8, app_storage, 8U) == EV_OK);

    assert(ev_actor_runtime_init(&boot_runtime, ACT_BOOT, &boot_mailbox, domain_handler, &boot_trace) == EV_OK);
    assert(ev_actor_runtime_init(&stream_runtime, ACT_STREAM, &stream_mailbox, domain_handler, &stream_trace) == EV_OK);
    assert(ev_actor_runtime_init(&app_runtime, ACT_APP, &app_mailbox, domain_handler, &app_trace) == EV_OK);

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &boot_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &stream_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &app_runtime) == EV_OK);
    assert(ev_domain_pump_init(&fast_pump, &registry, EV_DOMAIN_FAST_LOOP) == EV_OK);

    enqueue_boot(&boot_mailbox, 1U);
    enqueue_stream(&stream_mailbox, 1U);
    enqueue_app(&app_mailbox, 1U);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fast_pump, 2U, &report) == EV_OK);
    assert(report.pending_before == 3U);
    assert(report.processed == 2U);
    assert(report.pending_after == 1U);
    assert(report.last_actor == ACT_STREAM);
    assert(report.stop_result == EV_OK);
    assert(boot_trace.calls == 1U);
    assert(stream_trace.calls == 1U);
    assert(app_trace.calls == 0U);
}


int main(void)
{
    ev_msg_t boot_storage[8] = {{0}};
    ev_msg_t stream_storage[16] = {{0}};
    ev_msg_t app_storage[8] = {{0}};
    ev_msg_t diag_storage[8] = {{0}};
    ev_msg_t fail_boot_storage[8] = {{0}};
    ev_msg_t fail_stream_storage[16] = {{0}};
    ev_mailbox_t boot_mailbox;
    ev_mailbox_t stream_mailbox;
    ev_mailbox_t app_mailbox;
    ev_mailbox_t diag_mailbox;
    ev_mailbox_t fail_boot_mailbox;
    ev_mailbox_t fail_stream_mailbox;
    ev_actor_runtime_t boot_runtime;
    ev_actor_runtime_t stream_runtime;
    ev_actor_runtime_t app_runtime;
    ev_actor_runtime_t diag_runtime;
    ev_actor_runtime_t fail_boot_runtime;
    ev_actor_runtime_t fail_stream_runtime;
    ev_actor_registry_t registry = {0};
    ev_actor_registry_t fail_registry = {0};
    ev_domain_pump_t fast_pump;
    ev_domain_pump_t slow_pump;
    ev_domain_pump_t fail_pump;
    ev_domain_pump_report_t report;
    const ev_domain_pump_stats_t *stats;
    domain_trace_t boot_trace = {0};
    domain_trace_t stream_trace = {0};
    domain_trace_t app_trace = {0};
    domain_trace_t diag_trace = {0};
    domain_trace_t fail_boot_trace = {0};
    domain_trace_t fail_stream_trace = {0};

    domain_pump_cursor_regression();

    assert(ev_mailbox_init(&boot_mailbox, EV_MAILBOX_FIFO_8, boot_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&stream_mailbox, EV_MAILBOX_FIFO_16, stream_storage, 16U) == EV_OK);
    assert(ev_mailbox_init(&app_mailbox, EV_MAILBOX_FIFO_8, app_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&diag_mailbox, EV_MAILBOX_FIFO_8, diag_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&fail_boot_mailbox, EV_MAILBOX_FIFO_8, fail_boot_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&fail_stream_mailbox, EV_MAILBOX_FIFO_16, fail_stream_storage, 16U) == EV_OK);

    assert(ev_actor_runtime_init(&boot_runtime, ACT_BOOT, &boot_mailbox, domain_handler, &boot_trace) == EV_OK);
    assert(ev_actor_runtime_init(&stream_runtime, ACT_STREAM, &stream_mailbox, domain_handler, &stream_trace) == EV_OK);
    assert(ev_actor_runtime_init(&app_runtime, ACT_APP, &app_mailbox, domain_handler, &app_trace) == EV_OK);
    assert(ev_actor_runtime_init(&diag_runtime, ACT_DIAG, &diag_mailbox, domain_handler, &diag_trace) == EV_OK);
    assert(ev_actor_runtime_init(&fail_boot_runtime, ACT_BOOT, &fail_boot_mailbox, domain_handler, &fail_boot_trace) == EV_OK);
    fail_stream_trace.fail_on_call = 1U;
    fail_stream_trace.fail_result = EV_ERR_STATE;
    assert(ev_actor_runtime_init(&fail_stream_runtime, ACT_STREAM, &fail_stream_mailbox, domain_handler, &fail_stream_trace) == EV_OK);

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &boot_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &stream_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &app_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &diag_runtime) == EV_OK);

    assert(ev_domain_pump_init(&fast_pump, &registry, EV_DOMAIN_FAST_LOOP) == EV_OK);
    assert(ev_domain_pump_init(&slow_pump, &registry, EV_DOMAIN_SLOW_IO) == EV_OK);

    stats = ev_domain_pump_stats(&fast_pump);
    assert(stats != NULL);
    assert(stats->pump_calls == 0U);
    assert(stats->pump_empty_calls == 0U);
    assert(stats->pump_budget_hits == 0U);
    assert(stats->last_actor == EV_ACTOR_NONE);
    assert(stats->last_result == EV_OK);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fast_pump, 2U, &report) == EV_ERR_EMPTY);
    assert(report.budget == 2U);
    assert(report.processed == 0U);
    assert(report.pending_before == 0U);
    assert(report.pending_after == 0U);
    assert(report.actors_examined == 0U);
    assert(report.actors_pumped == 0U);
    assert(report.last_actor == EV_ACTOR_NONE);
    assert(report.stop_result == EV_ERR_EMPTY);

    stats = ev_domain_pump_stats(&fast_pump);
    assert(stats->pump_calls == 1U);
    assert(stats->pump_empty_calls == 1U);
    assert(stats->last_result == EV_ERR_EMPTY);

    enqueue_boot(&boot_mailbox, 1U);
    enqueue_stream(&stream_mailbox, 1U);
    enqueue_app(&app_mailbox, 1U);
    assert(ev_domain_pump_pending(&fast_pump) == 3U);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fast_pump, 1U, &report) == EV_OK);
    assert(report.budget == 1U);
    assert(report.pending_before == 3U);
    assert(report.processed == 1U);
    assert(report.pending_after == 2U);
    assert(report.actors_pumped == 1U);
    assert(report.exhausted_budget);
    assert(report.last_actor == ACT_BOOT);
    assert(report.stop_result == EV_OK);
    assert(boot_trace.calls == 1U);
    assert(stream_trace.calls == 0U);
    assert(app_trace.calls == 0U);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fast_pump, 1U, &report) == EV_OK);
    assert(report.pending_before == 2U);
    assert(report.processed == 1U);
    assert(report.pending_after == 1U);
    assert(report.exhausted_budget);
    assert(report.last_actor == ACT_STREAM);
    assert(stream_trace.calls == 1U);
    assert(app_trace.calls == 0U);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fast_pump, 1U, &report) == EV_OK);
    assert(report.pending_before == 1U);
    assert(report.processed == 1U);
    assert(report.pending_after == 0U);
    assert(!report.exhausted_budget);
    assert(report.last_actor == ACT_APP);
    assert(report.stop_result == EV_ERR_EMPTY);
    assert(app_trace.calls == 1U);
    assert(ev_domain_pump_pending(&fast_pump) == 0U);

    stats = ev_domain_pump_stats(&fast_pump);
    assert(stats->pump_calls == 4U);
    assert(stats->pump_empty_calls == 1U);
    assert(stats->pump_budget_hits == 2U);
    assert(stats->last_actor == ACT_APP);
    assert(stats->last_budget == 1U);
    assert(stats->last_processed == 1U);
    assert(stats->pending_high_watermark >= 3U);
    assert(stats->max_actors_examined_per_call >= 1U);
    assert(stats->max_actors_pumped_per_call >= 1U);
    assert(stats->max_messages_per_call >= 1U);
    assert(stats->last_result == EV_OK);

    enqueue_diag(&diag_mailbox, 2U);
    assert(ev_domain_pump_pending(&slow_pump) == 2U);
    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&slow_pump, 4U, &report) == EV_OK);
    assert(report.pending_before == 2U);
    assert(report.processed == 2U);
    assert(report.pending_after == 0U);
    assert(report.actors_pumped == 1U);
    assert(report.last_actor == ACT_DIAG);
    assert(report.stop_result == EV_ERR_EMPTY);
    assert(diag_trace.calls == 2U);

    assert(ev_actor_registry_init(&fail_registry) == EV_OK);
    assert(ev_actor_registry_bind(&fail_registry, &fail_boot_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&fail_registry, &fail_stream_runtime) == EV_OK);
    assert(ev_domain_pump_init(&fail_pump, &fail_registry, EV_DOMAIN_FAST_LOOP) == EV_OK);

    enqueue_boot(&fail_boot_mailbox, 1U);
    enqueue_stream(&fail_stream_mailbox, 1U);

    ev_domain_pump_report_reset(&report);
    assert(ev_domain_pump_run(&fail_pump, 4U, &report) == EV_ERR_STATE);
    assert(report.pending_before == 2U);
    assert(report.processed == 2U);
    assert(report.pending_after == 0U);
    assert(report.actors_pumped == 2U);
    assert(report.last_actor == ACT_STREAM);
    assert(report.stop_result == EV_ERR_STATE);
    assert(fail_boot_trace.calls == 1U);
    assert(fail_stream_trace.calls == 1U);

    stats = ev_domain_pump_stats(&fail_pump);
    assert(stats->pump_calls == 1U);
    assert(stats->pump_empty_calls == 0U);
    assert(stats->pump_budget_hits == 0U);
    assert(stats->last_actor == ACT_STREAM);
    assert(stats->last_budget == 4U);
    assert(stats->last_processed == 2U);
    assert(stats->last_result == EV_ERR_STATE);

    assert(ev_domain_pump_reset_stats(&fail_pump) == EV_OK);
    stats = ev_domain_pump_stats(&fail_pump);
    assert(stats->pump_calls == 0U);
    assert(stats->pump_empty_calls == 0U);
    assert(stats->pump_budget_hits == 0U);
    assert(stats->last_actor == EV_ACTOR_NONE);
    assert(stats->last_result == EV_OK);

    return 0;
}
