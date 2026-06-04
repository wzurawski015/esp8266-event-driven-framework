#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "ev/actor_runtime.h"
#include "ev/dispose.h"
#include "ev/mailbox.h"
#include "ev/msg.h"

#define ARRAY_COUNT(a_) (sizeof(a_) / sizeof((a_)[0]))

typedef struct {
    size_t retains;
    size_t releases;
} lease_trace_t;

static ev_result_t retain_count(void *ctx, const void *payload, size_t payload_size)
{
    lease_trace_t *trace = (lease_trace_t *)ctx;
    (void)payload;
    (void)payload_size;
    ++trace->retains;
    return EV_OK;
}

static ev_result_t retain_fail(void *ctx, const void *payload, size_t payload_size)
{
    (void)ctx;
    (void)payload;
    (void)payload_size;
    return EV_ERR_STATE;
}

static void release_count(void *ctx, const void *payload, size_t payload_size)
{
    lease_trace_t *trace = (lease_trace_t *)ctx;
    (void)payload;
    (void)payload_size;
    ++trace->releases;
}


static ev_msg_t make_empty_msg(ev_event_id_t event_id)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    assert(ev_msg_init_publish(&msg, event_id, ACT_APP) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    return msg;
}

static ev_msg_t make_inline_msg(ev_event_id_t event_id, unsigned char value)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    assert(ev_msg_init_publish(&msg, event_id, ACT_APP) == EV_OK);
    assert(ev_msg_set_inline_payload(&msg, &value, sizeof(value)) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    return msg;
}

static ev_msg_t make_lease_msg(lease_trace_t *trace, const void *payload, ev_msg_retain_fn_t retain_fn)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    assert(ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG) == EV_OK);
    assert(ev_msg_set_external_payload(&msg, payload, 2U, retain_fn, release_count, trace) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    return msg;
}

static void test_coalesced_repeated_event_does_not_grow_queue(void)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    ev_msg_t msg = make_empty_msg(EV_TICK_1S);
    ev_msg_t other = make_empty_msg(EV_TICK_100MS);
    ev_msg_t out = EV_MSG_INITIALIZER;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    assert(ev_mailbox_push_qos(&mailbox, &msg, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_POSTED);
    assert(report.queue_depth_before == 0U);
    assert(report.queue_depth_after == 1U);
    assert(ev_mailbox_push_qos(&mailbox, &msg, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_COALESCED);
    assert(report.queue_depth_before == 1U);
    assert(report.queue_depth_after == 1U);
    assert(ev_mailbox_push_qos(&mailbox, &msg, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_COALESCED);
    assert(ev_mailbox_count(&mailbox) == 1U);
    assert(ev_mailbox_stats(&mailbox)->posted == 1U);
    assert(ev_mailbox_stats(&mailbox)->coalesced == 2U);

    assert(ev_mailbox_push_qos(&mailbox, &other, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_POSTED);
    assert(ev_mailbox_count(&mailbox) == 2U);

    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(out.event_id == EV_TICK_1S);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(ev_mailbox_push_qos(&mailbox, &msg, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_POSTED);

    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_msg_dispose(&other) == EV_OK);
}

static void test_coalesced_full_matching_event_is_not_full(void)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    ev_msg_t msg = make_empty_msg(EV_TICK_1S);
    ev_msg_t other = make_empty_msg(EV_BOOT_STARTED);
    size_t i;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    for (i = 0U; i < ARRAY_COUNT(storage); ++i) {
        ev_msg_t fill = make_inline_msg((i == 0U) ? EV_TICK_1S : EV_TICK_100MS, (unsigned char)i);
        assert(ev_mailbox_push(&mailbox, &fill) == EV_OK);
        assert(ev_msg_dispose(&fill) == EV_OK);
    }
    assert(ev_mailbox_is_full(&mailbox));
    assert(ev_mailbox_push_qos(&mailbox, &msg, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_COALESCED);
    assert(ev_mailbox_count(&mailbox) == ARRAY_COUNT(storage));
    assert(ev_mailbox_push_qos(&mailbox, &other, EV_ROUTE_QOS_COALESCED, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_DROPPED);
    assert(ev_mailbox_count(&mailbox) == ARRAY_COUNT(storage));
    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_msg_dispose(&other) == EV_OK);
}

static void test_latest_only_replaces_pending_inline_payload(void)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    ev_msg_t first = make_inline_msg(EV_TICK_1S, 1U);
    ev_msg_t latest = make_inline_msg(EV_TICK_1S, 9U);
    ev_msg_t out = EV_MSG_INITIALIZER;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    assert(ev_mailbox_push_qos(&mailbox, &first, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_POSTED);
    assert(ev_mailbox_push_qos(&mailbox, &latest, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_REPLACED);
    assert(ev_mailbox_count(&mailbox) == 1U);
    assert(ev_mailbox_stats(&mailbox)->replaced == 1U);

    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(out.event_id == EV_TICK_1S);
    assert(*(const unsigned char *)ev_msg_payload_data(&out) == 9U);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(ev_msg_dispose(&first) == EV_OK);
    assert(ev_msg_dispose(&latest) == EV_OK);
}

static void test_latest_only_retain_release_for_lease_replacement(void)
{
    static const unsigned char old_payload[] = {0x10U, 0x11U};
    static const unsigned char new_payload[] = {0x20U, 0x21U};
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    lease_trace_t trace = {0U, 0U};
    ev_msg_t old_msg = make_lease_msg(&trace, old_payload, retain_count);
    ev_msg_t new_msg = make_lease_msg(&trace, new_payload, retain_count);
    ev_msg_t out = EV_MSG_INITIALIZER;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    assert(ev_mailbox_push_qos(&mailbox, &old_msg, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(trace.retains == 1U);
    assert(trace.releases == 0U);
    assert(ev_mailbox_push_qos(&mailbox, &new_msg, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_REPLACED);
    assert(trace.retains == 2U);
    assert(trace.releases == 1U);
    assert(ev_mailbox_count(&mailbox) == 1U);

    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(ev_msg_payload_data(&out) == new_payload);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(trace.releases == 2U);
    assert(ev_msg_dispose(&old_msg) == EV_OK);
    assert(ev_msg_dispose(&new_msg) == EV_OK);
    assert(trace.releases == 4U);
}

static void test_latest_only_retain_failure_preserves_old_slot(void)
{
    static const unsigned char old_payload[] = {0x30U, 0x31U};
    static const unsigned char new_payload[] = {0x40U, 0x41U};
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    lease_trace_t trace = {0U, 0U};
    ev_msg_t old_msg = make_lease_msg(&trace, old_payload, retain_count);
    ev_msg_t new_msg = make_lease_msg(&trace, new_payload, retain_fail);
    ev_msg_t out = EV_MSG_INITIALIZER;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    assert(ev_mailbox_push_qos(&mailbox, &old_msg, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(trace.retains == 1U);
    assert(ev_mailbox_push_qos(&mailbox, &new_msg, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_ERR_STATE);
    assert(report.effect == EV_MAILBOX_DELIVERY_REJECTED);
    assert(ev_mailbox_count(&mailbox) == 1U);
    assert(trace.retains == 1U);
    assert(trace.releases == 0U);

    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(ev_msg_payload_data(&out) == old_payload);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(ev_msg_dispose(&old_msg) == EV_OK);
    assert(ev_msg_dispose(&new_msg) == EV_OK);
}

static void test_latest_only_full_matching_event_replaces_not_full(void)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_mailbox_delivery_report_t report;
    ev_msg_t latest = make_inline_msg(EV_TICK_1S, 0xEEU);
    ev_msg_t missing = make_inline_msg(EV_BUTTON_EVENT, 0xBBU);
    ev_msg_t out = EV_MSG_INITIALIZER;
    size_t i;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    for (i = 0U; i < ARRAY_COUNT(storage); ++i) {
        ev_msg_t fill = make_inline_msg((i == 0U) ? EV_TICK_1S : EV_TICK_100MS, (unsigned char)i);
        assert(ev_mailbox_push(&mailbox, &fill) == EV_OK);
        assert(ev_msg_dispose(&fill) == EV_OK);
    }
    assert(ev_mailbox_push_qos(&mailbox, &latest, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_REPLACED);
    assert(ev_mailbox_count(&mailbox) == ARRAY_COUNT(storage));
    assert(ev_mailbox_push_qos(&mailbox, &missing, EV_ROUTE_QOS_LATEST_ONLY, &report) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_DROPPED);
    assert(ev_mailbox_count(&mailbox) == ARRAY_COUNT(storage));

    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(out.event_id == EV_TICK_1S);
    assert(*(const unsigned char *)ev_msg_payload_data(&out) == 0xEEU);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(ev_msg_dispose(&latest) == EV_OK);
    assert(ev_msg_dispose(&missing) == EV_OK);
}

static ev_result_t noop_handler(void *ctx, const ev_msg_t *msg)
{
    (void)ctx;
    (void)msg;
    return EV_OK;
}

static void test_actor_registry_delivery_qos_reports_effects(void)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_actor_runtime_t runtime;
    ev_actor_registry_t registry;
    ev_mailbox_delivery_report_t report;
    ev_msg_t msg = make_empty_msg(EV_TICK_1S);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, ARRAY_COUNT(storage)) == EV_OK);
    assert(ev_actor_runtime_init(&runtime, ACT_APP, &mailbox, noop_handler, NULL) == EV_OK);
    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &runtime) == EV_OK);

    assert(ev_actor_registry_delivery_qos(ACT_APP, &msg, EV_ROUTE_QOS_COALESCED, &report, &registry) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_POSTED);
    assert(ev_actor_registry_delivery_qos(ACT_APP, &msg, EV_ROUTE_QOS_COALESCED, &report, &registry) == EV_OK);
    assert(report.effect == EV_MAILBOX_DELIVERY_COALESCED);
    assert(ev_actor_registry_stats(&registry)->delivery_succeeded == 2U);
    assert(ev_actor_runtime_stats(&runtime)->enqueued == 2U);
    assert(ev_mailbox_count(&mailbox) == 1U);

    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

int main(void)
{
    test_coalesced_repeated_event_does_not_grow_queue();
    test_coalesced_full_matching_event_is_not_full();
    test_latest_only_replaces_pending_inline_payload();
    test_latest_only_retain_release_for_lease_replacement();
    test_latest_only_retain_failure_preserves_old_slot();
    test_latest_only_full_matching_event_replaces_not_full();
    test_actor_registry_delivery_qos_reports_effects();
    return 0;
}
