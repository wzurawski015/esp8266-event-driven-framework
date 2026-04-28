#include "ev/lease_pool.h"

#include <string.h>

static bool ev_lease_slot_metadata_is_valid(const ev_lease_slot_t *slot)
{
    if ((slot == NULL) || (slot->pool == NULL) || (slot->pool->slots == NULL) || (slot->pool->storage == NULL) ||
        (slot->pool->slot_count == 0U) || (slot->pool->slot_size == 0U) ||
        (slot->slot_index >= slot->pool->slot_count)) {
        return false;
    }
    if (slot != &slot->pool->slots[slot->slot_index]) {
        return false;
    }
    if (slot->in_use && (slot->payload_size > slot->pool->slot_size)) {
        return false;
    }

    return true;
}

static unsigned char *ev_lease_slot_data_ptr(const ev_lease_slot_t *slot)
{
    if (!ev_lease_slot_metadata_is_valid(slot)) {
        return NULL;
    }

    return slot->pool->storage + (slot->slot_index * slot->pool->slot_size);
}

static bool ev_lease_handle_is_live(const ev_lease_handle_t *handle)
{
    const ev_lease_slot_t *slot;

    if ((handle == NULL) || (handle->slot == NULL)) {
        return false;
    }

    slot = handle->slot;
    return ev_lease_slot_metadata_is_valid(slot) && slot->in_use && (slot->generation == handle->generation) &&
           (slot->refcount > 0U);
}

static ev_result_t ev_lease_pool_retain_from_slot(void *ctx, const void *payload, size_t payload_size)
{
    ev_lease_slot_t *slot = (ev_lease_slot_t *)ctx;
    ev_lease_handle_t handle;

    if (!ev_lease_slot_metadata_is_valid(slot) || !slot->in_use) {
        if ((slot != NULL) && (slot->pool != NULL)) {
            ++slot->pool->stats.stale_handles;
        }
        return EV_ERR_STATE;
    }
    if (((const void *)ev_lease_slot_data_ptr(slot) != payload) || (slot->payload_size != payload_size)) {
        ++slot->pool->stats.stale_handles;
        return EV_ERR_STATE;
    }

    handle.slot = slot;
    handle.generation = slot->generation;
    return ev_lease_pool_retain(&handle);
}

static void ev_lease_pool_release_from_slot(void *ctx, const void *payload, size_t payload_size)
{
    ev_lease_slot_t *slot = (ev_lease_slot_t *)ctx;
    ev_lease_handle_t handle;

    if (!ev_lease_slot_metadata_is_valid(slot) || !slot->in_use) {
        if ((slot != NULL) && (slot->pool != NULL)) {
            ++slot->pool->stats.stale_handles;
        }
        return;
    }
    if (((const void *)ev_lease_slot_data_ptr(slot) != payload) || (slot->payload_size != payload_size)) {
        ++slot->pool->stats.stale_handles;
        return;
    }

    handle.slot = slot;
    handle.generation = slot->generation;
    (void)ev_lease_pool_release(&handle);
}

ev_result_t ev_lease_pool_init(
    ev_lease_pool_t *pool,
    ev_lease_slot_t *slots,
    unsigned char *storage,
    size_t slot_count,
    size_t slot_size)
{
    size_t i;

    if ((pool == NULL) || (slots == NULL) || (storage == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((slot_count == 0U) || (slot_size == 0U)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    memset(pool, 0, sizeof(*pool));
    memset(slots, 0, sizeof(*slots) * slot_count);
    memset(storage, 0, slot_count * slot_size);

    pool->slots = slots;
    pool->storage = storage;
    pool->slot_count = slot_count;
    pool->slot_size = slot_size;

    for (i = 0U; i < slot_count; ++i) {
        pool->slots[i].pool = pool;
        pool->slots[i].slot_index = i;
    }

    return EV_OK;
}

ev_result_t ev_lease_pool_acquire(
    ev_lease_pool_t *pool,
    size_t payload_size,
    ev_lease_handle_t *out_handle,
    void **out_data)
{
    size_t i;
    ev_lease_slot_t *slot;

    if ((pool == NULL) || (out_handle == NULL) || (out_data == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (payload_size > pool->slot_size) {
        return EV_ERR_OUT_OF_RANGE;
    }

    for (i = 0U; i < pool->slot_count; ++i) {
        slot = &pool->slots[i];
        if (!slot->in_use) {
            memset(ev_lease_slot_data_ptr(slot), 0, pool->slot_size);
            slot->in_use = true;
            slot->refcount = 1U;
            slot->payload_size = payload_size;
            ++slot->generation;
            if (slot->generation == 0U) {
                slot->generation = 1U;
            }

            out_handle->slot = slot;
            out_handle->generation = slot->generation;
            *out_data = ev_lease_slot_data_ptr(slot);

            ++pool->stats.acquires;
            ++pool->stats.in_use;
            if (pool->stats.in_use > pool->stats.high_watermark) {
                pool->stats.high_watermark = pool->stats.in_use;
            }
            return EV_OK;
        }
    }

    ++pool->stats.failed_acquires;
    return EV_ERR_FULL;
}

bool ev_lease_handle_is_valid(const ev_lease_handle_t *handle)
{
    return ev_lease_handle_is_live(handle);
}

void *ev_lease_handle_data(const ev_lease_handle_t *handle)
{
    return ev_lease_handle_is_live(handle) ? ev_lease_slot_data_ptr(handle->slot) : NULL;
}

size_t ev_lease_handle_size(const ev_lease_handle_t *handle)
{
    return ev_lease_handle_is_live(handle) ? handle->slot->payload_size : 0U;
}

uint32_t ev_lease_handle_refcount(const ev_lease_handle_t *handle)
{
    return ev_lease_handle_is_live(handle) ? handle->slot->refcount : 0U;
}

ev_result_t ev_lease_pool_retain(const ev_lease_handle_t *handle)
{
    ev_lease_slot_t *slot;

    if (!ev_lease_handle_is_live(handle)) {
        if ((handle != NULL) && (handle->slot != NULL) && (handle->slot->pool != NULL)) {
            ++handle->slot->pool->stats.stale_handles;
        }
        return EV_ERR_STATE;
    }

    slot = handle->slot;
    ++slot->refcount;
    ++slot->pool->stats.retains;
    return EV_OK;
}

ev_result_t ev_lease_pool_release(const ev_lease_handle_t *handle)
{
    ev_lease_slot_t *slot;

    if (!ev_lease_handle_is_live(handle)) {
        if ((handle != NULL) && (handle->slot != NULL) && (handle->slot->pool != NULL)) {
            ++handle->slot->pool->stats.stale_handles;
        }
        return EV_ERR_STATE;
    }

    slot = handle->slot;
    --slot->refcount;
    ++slot->pool->stats.releases;

    if (slot->refcount == 0U) {
        memset(ev_lease_slot_data_ptr(slot), 0, slot->pool->slot_size);
        slot->payload_size = 0U;
        slot->in_use = false;
        --slot->pool->stats.in_use;
    }

    return EV_OK;
}

const ev_lease_pool_stats_t *ev_lease_pool_stats(const ev_lease_pool_t *pool)
{
    return (pool != NULL) ? &pool->stats : NULL;
}

ev_result_t ev_lease_pool_attach_msg(ev_msg_t *msg, const ev_lease_handle_t *handle)
{
    ev_result_t rc;
    void *data;
    size_t size;

    if ((msg == NULL) || (handle == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_lease_handle_is_live(handle)) {
        if ((handle->slot != NULL) && (handle->slot->pool != NULL)) {
            ++handle->slot->pool->stats.stale_handles;
        }
        return EV_ERR_STATE;
    }

    data = ev_lease_handle_data(handle);
    size = ev_lease_handle_size(handle);
    if ((data == NULL) && (size > 0U)) {
        return EV_ERR_STATE;
    }

    if (size > 0U) {
        rc = ev_lease_pool_retain(handle);
        if (rc != EV_OK) {
            return rc;
        }
    }

    rc = ev_msg_set_external_payload(
        msg,
        (size > 0U) ? data : NULL,
        size,
        (size > 0U) ? ev_lease_pool_retain_from_slot : NULL,
        (size > 0U) ? ev_lease_pool_release_from_slot : NULL,
        handle->slot);
    if (rc != EV_OK) {
        if (size > 0U) {
            (void)ev_lease_pool_release(handle);
        }
        return rc;
    }

    return EV_OK;
}
