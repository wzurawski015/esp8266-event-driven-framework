#include <assert.h>
#include <stddef.h>

#include "ev/dispose.h"
#include "ev/mailbox.h"
#include "ev/msg.h"

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

static void release_count(void *ctx, const void *payload, size_t payload_size)
{
    lease_trace_t *trace = (lease_trace_t *)ctx;
    (void)payload;
    (void)payload_size;
    ++trace->releases;
}

static ev_result_t retain_fail(void *ctx, const void *payload, size_t payload_size)
{
    (void)ctx;
    (void)payload;
    (void)payload_size;
    return EV_ERR_STATE;
}

static ev_msg_t make_tick_msg(void)
{
    ev_msg_t msg = {0};
    const unsigned char payload[] = {0x01U};

    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_BOOT) == EV_OK);
    assert(ev_msg_set_inline_payload(&msg, payload, sizeof(payload)) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    return msg;
}

static ev_msg_t make_lease_msg(lease_trace_t *trace, ev_msg_retain_fn_t retain_fn)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    static const unsigned char lease_bytes[] = {0xAAU, 0xBBU};

    assert(ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG) == EV_OK);
    assert(ev_msg_set_external_payload(
               &msg,
               lease_bytes,
               sizeof(lease_bytes),
               retain_fn,
               release_count,
               trace) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    return msg;
}

int main(void)
{
    ev_msg_t fifo_storage[8] = {{0}};
    ev_msg_t fifo16_storage[16] = {{0}};
    ev_msg_t one_storage[1] = {{0}};
    ev_msg_t lossy_storage[8] = {{0}};
    ev_msg_t flag_storage[1] = {{0}};
    ev_mailbox_t mailbox;
    ev_msg_t out = {0};
    ev_msg_t msg = make_tick_msg();
    lease_trace_t lease_trace = {0};
    size_t i;

    assert(ev_mailbox_kind_capacity(EV_MAILBOX_FIFO_8) == 8U);
    assert(ev_mailbox_kind_capacity(EV_MAILBOX_FIFO_16) == 16U);
    assert(ev_mailbox_kind_capacity(EV_MAILBOX_MAILBOX_1) == 1U);
    assert(ev_mailbox_kind_capacity(EV_MAILBOX_LOSSY_RING_8) == 8U);
    assert(ev_mailbox_kind_capacity(EV_MAILBOX_COALESCED_FLAG) == 1U);
    assert(ev_mailbox_kind_is_lossy(EV_MAILBOX_LOSSY_RING_8));
    assert(ev_mailbox_kind_is_coalescing(EV_MAILBOX_COALESCED_FLAG));

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, fifo_storage, 8U) == EV_OK);
    for (i = 0U; i < 8U; ++i) {
        assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    }
    assert(ev_mailbox_push(&mailbox, &msg) == EV_ERR_FULL);
    assert(ev_mailbox_is_full(&mailbox));
    assert(ev_mailbox_stats(&mailbox)->rejected == 1U);
    for (i = 0U; i < 8U; ++i) {
        assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
        assert(out.event_id == EV_TICK_1S);
    }
    assert(ev_mailbox_pop(&mailbox, &out) == EV_ERR_EMPTY);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_16, fifo16_storage, 16U) == EV_OK);
    assert(ev_mailbox_capacity(&mailbox) == 16U);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_MAILBOX_1, one_storage, 1U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_mailbox_count(&mailbox) == 1U);
    assert(ev_mailbox_stats(&mailbox)->replaced == 1U);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_LOSSY_RING_8, lossy_storage, 8U) == EV_OK);
    for (i = 0U; i < 9U; ++i) {
        assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    }
    assert(ev_mailbox_count(&mailbox) == 8U);
    assert(ev_mailbox_stats(&mailbox)->dropped == 1U);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_COALESCED_FLAG, flag_storage, 1U) == EV_OK);
    assert(ev_msg_init_publish(&msg, EV_BOOT_STARTED, ACT_BOOT) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_mailbox_stats(&mailbox)->coalesced == 1U);
    assert(ev_mailbox_count(&mailbox) == 1U);
    assert(ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_ERR_CONTRACT);

    /* Lease payloads are retainable and queue-safe in this stage. */
    msg = make_lease_msg(&lease_trace, retain_count);
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, fifo_storage, 8U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(lease_trace.retains == 1U);
    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(out.event_id == EV_DIAG_SNAPSHOT_RSP);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(lease_trace.releases == 1U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 2U);

    /* Replacing a retained message releases the mailbox-owned share. */
    msg = make_lease_msg(&lease_trace, retain_count);
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_MAILBOX_1, one_storage, 1U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(lease_trace.retains == 3U);
    assert(lease_trace.releases == 3U);
    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(lease_trace.releases == 4U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 5U);

    /* Drop-oldest must release the dropped mailbox-owned share. */
    msg = make_lease_msg(&lease_trace, retain_count);
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_LOSSY_RING_8, lossy_storage, 8U) == EV_OK);
    for (i = 0U; i < 9U; ++i) {
        assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    }
    assert(ev_mailbox_stats(&mailbox)->dropped == 1U);
    assert(lease_trace.retains == 12U);
    assert(lease_trace.releases == 6U);
    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(lease_trace.releases == 14U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 15U);

    /* Malformed lease envelopes are rejected before queueing. */
    msg = make_lease_msg(&lease_trace, retain_count);
    msg.payload.external.retain_fn = NULL;
    assert(ev_msg_validate(&msg) == EV_ERR_CONTRACT);
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, fifo_storage, 8U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_ERR_CONTRACT);
    assert(ev_mailbox_count(&mailbox) == 0U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 16U);

    /* Explicit retain failure must reject enqueue without mutating queue state. */
    msg = make_lease_msg(&lease_trace, retain_fail);
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, fifo_storage, 8U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_ERR_STATE);
    assert(ev_mailbox_count(&mailbox) == 0U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 17U);

    return 0;
}
