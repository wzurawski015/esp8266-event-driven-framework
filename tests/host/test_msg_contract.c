#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"

typedef struct {
    size_t retains;
    size_t releases;
} lease_trace_t;

static ev_result_t retain_counter(void *lifecycle_ctx, const void *payload, size_t payload_size)
{
    lease_trace_t *trace = (lease_trace_t *)lifecycle_ctx;
    (void)payload;
    (void)payload_size;
    ++trace->retains;
    return EV_OK;
}

static void release_counter(void *lifecycle_ctx, const void *payload, size_t payload_size)
{
    lease_trace_t *trace = (lease_trace_t *)lifecycle_ctx;
    (void)payload;
    (void)payload_size;
    ++trace->releases;
}

int main(void)
{
    ev_msg_t msg = {0};
    const unsigned char ok_bytes[] = {0x4fU, 0x4bU};
    const unsigned char lease_bytes[] = {0x01U, 0x02U, 0x03U};
    const unsigned char replacement_bytes[] = {0xAAU, 0xBBU};
    lease_trace_t lease_trace = {0};

    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_msg_is_disposed(&msg));
    assert(ev_msg_dispose(&msg) == EV_OK);

    ev_msg_reset(&msg);
    assert(ev_msg_init_send(&msg, EV_TICK_1S, ACT_BOOT, ACT_DIAG) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    assert(msg.target_actor == ACT_DIAG);

    assert(ev_msg_init_publish(&msg, EV_BOOT_STARTED, ACT_BOOT) == EV_OK);
    assert(msg.target_actor == EV_ACTOR_NONE);
    assert(ev_msg_set_inline_payload(&msg, ok_bytes, sizeof(ok_bytes)) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    assert(ev_msg_payload_size(&msg) == sizeof(ok_bytes));
    assert(memcmp(ev_msg_payload_data(&msg), ok_bytes, sizeof(ok_bytes)) == 0);

    assert(ev_msg_init_publish(&msg, EV_STREAM_CHUNK_READY, ACT_STREAM) == EV_OK);
    assert(ev_msg_set_inline_payload(&msg, ok_bytes, sizeof(ok_bytes)) == EV_ERR_CONTRACT);
    assert(ev_msg_set_external_payload(&msg, lease_bytes, sizeof(lease_bytes), NULL, NULL, NULL) == EV_ERR_CONTRACT);
    assert(ev_msg_set_external_payload(
               &msg,
               lease_bytes,
               sizeof(lease_bytes),
               NULL,
               release_counter,
               &lease_trace) == EV_ERR_CONTRACT);
    assert(ev_msg_set_external_payload(
               &msg,
               lease_bytes,
               sizeof(lease_bytes),
               retain_counter,
               NULL,
               &lease_trace) == EV_ERR_CONTRACT);
    assert(ev_msg_set_external_payload(
               &msg,
               lease_bytes,
               sizeof(lease_bytes),
               retain_counter,
               release_counter,
               &lease_trace) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    msg.payload.external.size -= 1U;
    assert(ev_msg_validate(&msg) == EV_ERR_CONTRACT);
    msg.payload.external.size = sizeof(lease_bytes);
    assert(ev_msg_validate(&msg) == EV_OK);
    assert(ev_msg_retain(&msg) == EV_OK);
    assert(lease_trace.retains == 1U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 1U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 1U);

    /* Replacing an external payload releases the previous attachment. */
    assert(ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG) == EV_OK);
    assert(ev_msg_set_external_payload(
               &msg,
               lease_bytes,
               sizeof(lease_bytes),
               retain_counter,
               release_counter,
               &lease_trace) == EV_OK);
    assert(ev_msg_set_external_payload(
               &msg,
               replacement_bytes,
               sizeof(replacement_bytes),
               retain_counter,
               release_counter,
               &lease_trace) == EV_OK);
    assert(lease_trace.releases == 2U);

    /* Reuse now makes ownership explicit: dispose first, then blind-init. */
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_trace.releases == 3U);

    memset(&msg, 0xA5, sizeof(msg));
    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    assert(lease_trace.releases == 3U);
    assert(msg.target_actor == ACT_DIAG);
    assert(ev_msg_validate(&msg) == EV_OK);

    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, EV_ACTOR_NONE) == EV_ERR_OUT_OF_RANGE);

    return 0;
}
