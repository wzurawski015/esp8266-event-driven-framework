#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/lease_pool.h"
#include "ev/mailbox.h"
#include "ev/msg.h"

static void test_inline_contract_helpers(void)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    const unsigned char payload[] = {0x10U, 0x20U, 0x30U};

    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP) == EV_OK);
    assert(ev_msg_payload_is_inline_contract(&msg));
    assert(!ev_msg_payload_is_zero_copy_contract(&msg));
    assert(!ev_msg_payload_is_lease_contract(&msg));
    assert(!ev_msg_payload_is_stream_view_contract(&msg));
    assert(!ev_msg_payload_requires_release(&msg));
    assert(ev_msg_set_inline_payload(&msg, payload, sizeof(payload)) == EV_OK);
    assert(ev_msg_validate_payload_contract(&msg) == EV_OK);
    assert(ev_msg_payload_data(&msg) != payload);
    assert(memcmp(ev_msg_payload_data(&msg), payload, sizeof(payload)) == 0);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_lease_payload_fanout_uses_retain_release_contract(void)
{
    ev_lease_pool_t pool;
    ev_lease_slot_t slots[1] = {{0}};
    unsigned char storage[32] = {0};
    ev_lease_handle_t handle = {0};
    void *data = NULL;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_mailbox_t first_mailbox;
    ev_mailbox_t second_mailbox;
    ev_msg_t first_storage[8] = {{0}};
    ev_msg_t second_storage[8] = {{0}};
    ev_msg_t first_out = EV_MSG_INITIALIZER;
    ev_msg_t second_out = EV_MSG_INITIALIZER;
    static const unsigned char payload[] = {0xA1U, 0xA2U, 0xA3U, 0xA4U};

    assert(ev_lease_pool_init(&pool, slots, storage, 1U, sizeof(storage)) == EV_OK);
    assert(ev_lease_pool_acquire(&pool, sizeof(payload), &handle, &data) == EV_OK);
    assert(data != NULL);
    memcpy(data, payload, sizeof(payload));

    assert(ev_msg_init_publish(&msg, EV_NET_MQTT_MSG_RX_LEASE, ACT_NETWORK) == EV_OK);
    assert(ev_msg_payload_is_lease_contract(&msg));
    assert(ev_msg_payload_is_zero_copy_contract(&msg));
    assert(!ev_msg_payload_requires_release(&msg));
    assert(ev_lease_pool_attach_msg(&msg, &handle) == EV_OK);
    assert(ev_msg_payload_requires_release(&msg));
    assert(ev_msg_validate_payload_contract(&msg) == EV_OK);
    assert(ev_msg_payload_data(&msg) == data);
    assert(ev_lease_handle_refcount(&handle) == 2U);

    assert(ev_mailbox_init(&first_mailbox, EV_MAILBOX_FIFO_8, first_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&second_mailbox, EV_MAILBOX_FIFO_8, second_storage, 8U) == EV_OK);
    assert(ev_mailbox_push(&first_mailbox, &msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle) == 3U);
    assert(ev_mailbox_push(&second_mailbox, &msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle) == 4U);

    assert(ev_mailbox_pop(&first_mailbox, &first_out) == EV_OK);
    assert(ev_mailbox_pop(&second_mailbox, &second_out) == EV_OK);
    assert(ev_msg_payload_data(&first_out) == data);
    assert(ev_msg_payload_data(&second_out) == data);
    assert(memcmp(ev_msg_payload_data(&first_out), payload, sizeof(payload)) == 0);

    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(ev_lease_handle_refcount(&handle) == 3U);
    assert(ev_msg_dispose(&first_out) == EV_OK);
    assert(ev_lease_handle_refcount(&handle) == 2U);
    assert(ev_msg_dispose(&second_out) == EV_OK);
    assert(ev_lease_handle_refcount(&handle) == 1U);
    assert(ev_lease_pool_release(&handle) == EV_OK);
    assert(!ev_lease_handle_is_valid(&handle));
    assert(ev_lease_pool_stats(&pool)->in_use == 0U);
}

static void test_lease_pool_exhaustion_is_bounded_without_heap(void)
{
    ev_lease_pool_t pool;
    ev_lease_slot_t slots[1] = {{0}};
    unsigned char storage[16] = {0};
    ev_lease_handle_t first = {0};
    ev_lease_handle_t second = {0};
    void *data = NULL;

    assert(ev_lease_pool_init(&pool, slots, storage, 1U, sizeof(storage)) == EV_OK);
    assert(ev_lease_pool_acquire(&pool, 1U, &first, &data) == EV_OK);
    assert(ev_lease_pool_acquire(&pool, 1U, &second, &data) == EV_ERR_FULL);
    assert(ev_lease_pool_stats(&pool)->failed_acquires == 1U);
    assert(ev_lease_pool_release(&first) == EV_OK);
    assert(ev_lease_pool_release(&first) == EV_ERR_STATE);
    assert(ev_lease_pool_stats(&pool)->stale_handles == 1U);
}

static void test_borrowed_payload_cannot_impersonate_lease(void)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    static const unsigned char borrowed[] = {0x55U, 0x66U};

    assert(ev_msg_init_publish(&msg, EV_NET_MQTT_MSG_RX_LEASE, ACT_NETWORK) == EV_OK);
    assert(ev_msg_set_external_payload(&msg, borrowed, sizeof(borrowed), NULL, NULL, NULL) == EV_ERR_CONTRACT);
    assert(ev_msg_set_inline_payload(&msg, borrowed, sizeof(borrowed)) == EV_ERR_CONTRACT);
    assert(ev_msg_validate_payload_contract(&msg) == EV_OK);
    assert(ev_msg_payload_size(&msg) == 0U);
}

int main(void)
{
    test_inline_contract_helpers();
    test_lease_payload_fanout_uses_retain_release_contract();
    test_lease_pool_exhaustion_is_bounded_without_heap();
    test_borrowed_payload_cannot_impersonate_lease();
    return 0;
}
