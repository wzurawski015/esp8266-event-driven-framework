#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/lease_pool.h"
#include "ev/mailbox.h"
#include "ev/msg.h"

int main(void)
{
    ev_lease_pool_t pool;
    ev_lease_slot_t slots[2] = {{0}};
    unsigned char storage[2U * 16U] = {0};
    ev_lease_handle_t handle_a = {0};
    ev_lease_handle_t handle_b = {0};
    ev_lease_handle_t handle_c = {0};
    ev_lease_handle_t handle_zero = {0};
    void *data = NULL;
    ev_msg_t mailbox_storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_msg_t msg = {0};
    ev_msg_t out = {0};
    const ev_lease_pool_stats_t *stats;
    static const unsigned char payload_bytes[] = {0x11U, 0x22U, 0x33U};

    assert(ev_lease_pool_init(&pool, slots, storage, 2U, 16U) == EV_OK);
    stats = ev_lease_pool_stats(&pool);
    assert(stats != NULL);
    assert(stats->in_use == 0U);

    assert(ev_lease_pool_acquire(&pool, sizeof(payload_bytes), &handle_a, &data) == EV_OK);
    assert(data != NULL);
    memcpy(data, payload_bytes, sizeof(payload_bytes));
    assert(ev_lease_handle_is_valid(&handle_a));
    assert(ev_lease_handle_refcount(&handle_a) == 1U);
    assert(ev_lease_handle_size(&handle_a) == sizeof(payload_bytes));
    assert(memcmp(ev_lease_handle_data(&handle_a), payload_bytes, sizeof(payload_bytes)) == 0);
    assert(stats->acquires == 1U);
    assert(stats->in_use == 1U);
    assert(stats->high_watermark == 1U);

    assert(ev_lease_pool_retain(&handle_a) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 2U);
    assert(stats->retains == 1U);
    assert(ev_lease_pool_release(&handle_a) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 1U);
    assert(stats->releases == 1U);

    {
        ev_lease_slot_t forged_slot = *handle_a.slot;
        ev_lease_handle_t forged_handle = {&forged_slot, forged_slot.generation};
        assert(ev_lease_pool_retain(&forged_handle) == EV_ERR_STATE);
        assert(ev_lease_handle_refcount(&handle_a) == 1U);
        assert(stats->stale_handles == 1U);
    }

    assert(ev_lease_pool_acquire(&pool, 4U, &handle_b, &data) == EV_OK);
    assert(ev_lease_handle_is_valid(&handle_b));
    assert(stats->in_use == 2U);
    assert(stats->high_watermark == 2U);
    assert(ev_lease_pool_acquire(&pool, 1U, &handle_c, &data) == EV_ERR_FULL);
    assert(stats->failed_acquires == 1U);
    assert(ev_lease_pool_release(&handle_b) == EV_OK);
    assert(stats->in_use == 1U);

    assert(ev_lease_pool_acquire(&pool, 0U, &handle_zero, &data) == EV_OK);
    assert(ev_lease_handle_is_valid(&handle_zero));
    assert(ev_lease_handle_size(&handle_zero) == 0U);
    assert(ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG) == EV_OK);
    assert(ev_lease_pool_attach_msg(&msg, &handle_zero) == EV_OK);
    assert(ev_msg_validate(&msg) == EV_OK);
    assert(ev_msg_payload_size(&msg) == 0U);
    assert(ev_lease_handle_refcount(&handle_zero) == 1U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_zero) == 1U);
    assert(ev_lease_pool_release(&handle_zero) == EV_OK);
    assert(!ev_lease_handle_is_valid(&handle_zero));

    assert(ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG) == EV_OK);
    assert(ev_lease_pool_attach_msg(&msg, &handle_a) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 2U);
    assert(ev_msg_validate(&msg) == EV_OK);
    assert(ev_msg_payload_size(&msg) == sizeof(payload_bytes));
    assert(memcmp(ev_msg_payload_data(&msg), payload_bytes, sizeof(payload_bytes)) == 0);

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, mailbox_storage, 8U) == EV_OK);
    assert(ev_mailbox_push(&mailbox, &msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 3U);
    assert(ev_mailbox_pop(&mailbox, &out) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 2U);
    assert(ev_msg_dispose(&out) == EV_OK);
    assert(ev_lease_handle_refcount(&handle_a) == 1U);

    assert(ev_lease_pool_release(&handle_a) == EV_OK);
    assert(!ev_lease_handle_is_valid(&handle_a));
    assert(stats->in_use == 0U);
    assert(ev_lease_pool_release(&handle_a) == EV_ERR_STATE);
    assert(stats->stale_handles == 2U);

    return 0;
}
