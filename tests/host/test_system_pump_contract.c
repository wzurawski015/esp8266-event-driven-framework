#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/actor_runtime.h"
#include "ev/domain_pump.h"
#include "ev/mailbox.h"
#include "ev/msg.h"
#include "ev/system_pump.h"

typedef struct {
    uint32_t calls;
    uint32_t fail_on_call;
    ev_result_t fail_result;
} system_trace_t;

static ev_result_t system_handler(void *context, const ev_msg_t *msg)
{
    system_trace_t *trace = (system_trace_t *)context;
    assert(trace != NULL);
    assert(msg != NULL);
    ++trace->calls;
    if ((trace->fail_on_call != 0U) && (trace->calls == trace->fail_on_call)) {
        return trace->fail_result;
    }
    return EV_OK;
}

static void test_system_pump_round_robin(void)
{
    ev_actor_registry_t registry = {0};
    ev_mailbox_t boot_mailbox;
    ev_mailbox_t diag_mailbox;
    ev_mailbox_t app_mailbox;
    ev_msg_t boot_storage[8] = {{0}};
    ev_msg_t diag_storage[8] = {{0}};
    ev_msg_t app_storage[8] = {{0}};
    ev_actor_runtime_t boot_runtime;
    ev_actor_runtime_t diag_runtime;
    ev_actor_runtime_t app_runtime;
    ev_domain_pump_t fast_domain;
    ev_domain_pump_t slow_domain;
    ev_system_pump_t system_pump;
    ev_system_pump_report_t report;
    system_trace_t boot_trace = {0};
    system_trace_t diag_trace = {0};
    system_trace_t app_trace = {0};
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t i;

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_mailbox_init(&boot_mailbox, EV_MAILBOX_FIFO_8, boot_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&diag_mailbox, EV_MAILBOX_FIFO_8, diag_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&app_mailbox, EV_MAILBOX_FIFO_8, app_storage, 8U) == EV_OK);

    assert(ev_actor_runtime_init(&boot_runtime, ACT_BOOT, &boot_mailbox, system_handler, &boot_trace) == EV_OK);
    assert(ev_actor_runtime_init(&diag_runtime, ACT_DIAG, &diag_mailbox, system_handler, &diag_trace) == EV_OK);
    assert(ev_actor_runtime_init(&app_runtime, ACT_APP, &app_mailbox, system_handler, &app_trace) == EV_OK);

    assert(ev_actor_registry_bind(&registry, &boot_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &diag_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &app_runtime) == EV_OK);

    assert(ev_domain_pump_init(&fast_domain, &registry, EV_DOMAIN_FAST_LOOP) == EV_OK);
    assert(ev_domain_pump_init(&slow_domain, &registry, EV_DOMAIN_SLOW_IO) == EV_OK);

    assert(ev_system_pump_init(&system_pump) == EV_OK);
    assert(ev_system_pump_bind(&system_pump, &fast_domain) == EV_OK);
    assert(ev_system_pump_bind(&system_pump, &slow_domain) == EV_OK);
    assert(ev_system_pump_bound_count(&system_pump) == 2U);

    for (i = 0U; i < 2U; ++i) {
        assert(ev_msg_init_send(&msg, EV_BOOT_COMPLETED, ACT_DIAG, ACT_BOOT) == EV_OK);
        assert(ev_actor_registry_delivery(ACT_BOOT, &msg, &registry) == EV_OK);
    }

    for (i = 0U; i < 8U; ++i) {
        assert(ev_msg_init_send(&msg, EV_BOOT_COMPLETED, ACT_APP, ACT_APP) == EV_OK);
        assert(ev_actor_registry_delivery(ACT_APP, &msg, &registry) == EV_OK);
    }

    for (i = 0U; i < 3U; ++i) {
        assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
        assert(ev_actor_registry_delivery(ACT_DIAG, &msg, &registry) == EV_OK);
    }

    assert(ev_system_pump_pending(&system_pump) == 13U);
    assert(ev_system_pump_run(&system_pump, 2U, &report) == EV_OK);
    assert(report.turn_budget == 2U);
    assert(report.turns_processed == 2U);
    assert(report.domains_pumped == 2U);
    assert(report.messages_processed == 13U);
    assert(report.exhausted_turn_budget == false);
    assert(ev_system_pump_pending(&system_pump) == 0U);
    assert(boot_trace.calls == 2U);
    assert(diag_trace.calls == 3U);
    assert(app_trace.calls == 8U);

    /* All pending work drained in the first round-robin pass. */
    assert(ev_system_pump_run(&system_pump, 4U, &report) == EV_ERR_EMPTY);
    assert(report.turns_processed == 0U);
    assert(ev_system_pump_pending(&system_pump) == 0U);

    {
        const ev_system_pump_stats_t *stats = ev_system_pump_stats(&system_pump);
        assert(stats != NULL);
        assert(stats->run_calls == 2U);
        assert(stats->domains_pumped == 2U);
        assert(stats->messages_processed == 13U);
        assert(stats->budget_hits == 0U);
        assert(stats->pending_high_watermark == 13U);
        assert(stats->max_domains_examined_per_call >= 2U);
        assert(stats->max_domains_pumped_per_call >= 2U);
        assert(stats->max_turns_per_call >= 2U);
        assert(stats->max_messages_per_call == 13U);
        assert(stats->last_result == EV_ERR_EMPTY);
    }
}

static void test_system_pump_error_counts_failed_message(void)
{
    ev_actor_registry_t registry = {0};
    ev_mailbox_t diag_mailbox;
    ev_msg_t diag_storage[8] = {{0}};
    ev_actor_runtime_t diag_runtime;
    ev_domain_pump_t slow_domain;
    ev_system_pump_t system_pump;
    ev_system_pump_report_t report;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    system_trace_t diag_trace = {0};

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_mailbox_init(&diag_mailbox, EV_MAILBOX_FIFO_8, diag_storage, 8U) == EV_OK);
    diag_trace.fail_on_call = 1U;
    diag_trace.fail_result = EV_ERR_STATE;
    assert(ev_actor_runtime_init(&diag_runtime, ACT_DIAG, &diag_mailbox, system_handler, &diag_trace) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &diag_runtime) == EV_OK);

    assert(ev_domain_pump_init(&slow_domain, &registry, EV_DOMAIN_SLOW_IO) == EV_OK);
    assert(ev_system_pump_init(&system_pump) == EV_OK);
    assert(ev_system_pump_bind(&system_pump, &slow_domain) == EV_OK);

    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    assert(ev_actor_registry_delivery(ACT_DIAG, &msg, &registry) == EV_OK);

    assert(ev_system_pump_run(&system_pump, 1U, &report) == EV_ERR_STATE);
    assert(report.turn_budget == 1U);
    assert(report.turns_processed == 1U);
    assert(report.domains_pumped == 1U);
    assert(report.messages_processed == 1U);
    assert(report.pending_before == 1U);
    assert(report.pending_after == 0U);
    assert(!report.exhausted_turn_budget);
    assert(report.last_domain == EV_DOMAIN_SLOW_IO);
    assert(report.stop_result == EV_ERR_STATE);
    assert(diag_trace.calls == 1U);

    {
        const ev_system_pump_stats_t *stats = ev_system_pump_stats(&system_pump);
        assert(stats != NULL);
        assert(stats->run_calls == 1U);
        assert(stats->domains_pumped == 1U);
        assert(stats->turns_processed == 1U);
        assert(stats->messages_processed == 1U);
        assert(stats->last_turns_processed == 1U);
        assert(stats->last_domain == EV_DOMAIN_SLOW_IO);
        assert(stats->last_result == EV_ERR_STATE);
    }
}

static void test_system_pump_empty_and_bind_errors(void)
{
    ev_system_pump_t system_pump;
    ev_domain_pump_t domain_pump;
    ev_actor_registry_t registry = {0};
    ev_system_pump_report_t report;

    assert(ev_system_pump_init(NULL) == EV_ERR_INVALID_ARG);
    assert(ev_system_pump_init(&system_pump) == EV_OK);
    assert(ev_system_pump_run(&system_pump, 1U, &report) == EV_ERR_STATE);
    assert(ev_system_pump_bind(&system_pump, NULL) == EV_ERR_INVALID_ARG);

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_domain_pump_init(&domain_pump, &registry, EV_DOMAIN_FAST_LOOP) == EV_OK);
    assert(ev_system_pump_bind(&system_pump, &domain_pump) == EV_OK);
    assert(ev_system_pump_bind(&system_pump, &domain_pump) == EV_ERR_STATE);
    assert(ev_system_pump_run(&system_pump, 1U, &report) == EV_ERR_EMPTY);

    {
        const ev_system_pump_stats_t *stats = ev_system_pump_stats(&system_pump);
        assert(stats != NULL);
        assert(stats->run_calls == 1U);
        assert(stats->empty_calls == 1U);
        assert(stats->last_result == EV_ERR_EMPTY);
    }
}

int main(void)
{
    test_system_pump_round_robin();
    test_system_pump_error_counts_failed_message();
    test_system_pump_empty_and_bind_errors();
    return 0;
}
