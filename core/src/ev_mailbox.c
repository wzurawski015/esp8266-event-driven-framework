#include "ev/mailbox.h"

#include <string.h>

#include "ev/compiler.h"
#include "ev/dispose.h"

static bool ev_mailbox_kind_is_known(ev_mailbox_kind_t kind)
{
    return ev_mailbox_kind_capacity(kind) > 0U;
}

static void ev_mailbox_record_high_watermark(ev_mailbox_t *mailbox)
{
    if ((mailbox != NULL) && (mailbox->count > mailbox->stats.high_watermark)) {
        mailbox->stats.high_watermark = mailbox->count;
    }
}

static bool ev_mailbox_capacity_is_power_of_two(size_t storage_count)
{
    return (storage_count != 0U) && ((storage_count & (storage_count - 1U)) == 0U);
}

static size_t ev_mailbox_index_advance(const ev_mailbox_t *mailbox, size_t index)
{
    return (index + 1U) & mailbox->storage_mask;
}

static ev_result_t ev_mailbox_dispose_slot(ev_mailbox_t *mailbox, size_t index)
{
    ev_result_t rc;

    if ((mailbox == NULL) || (mailbox->storage == NULL) || (index >= mailbox->storage_count)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_dispose(&mailbox->storage[index]);
    ev_msg_reset(&mailbox->storage[index]);
    return rc;
}

static ev_result_t ev_mailbox_store_at(ev_mailbox_t *mailbox, size_t index, const ev_msg_t *msg)
{
    if ((mailbox == NULL) || (msg == NULL) || (index >= mailbox->storage_count)) {
        return EV_ERR_INVALID_ARG;
    }

    mailbox->storage[index] = *msg;
    return EV_OK;
}

static ev_result_t ev_mailbox_retain_for_queue(const ev_msg_t *msg)
{
    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if ((msg->storage != EV_MSG_STORAGE_EXTERNAL) || (msg->payload_size == 0U)) {
        return EV_OK;
    }

    return ev_msg_retain(msg);
}

size_t ev_mailbox_kind_capacity(ev_mailbox_kind_t kind)
{
    switch (kind) {
    case EV_MAILBOX_FIFO_8:
        return 8U;
    case EV_MAILBOX_FIFO_16:
        return 16U;
    case EV_MAILBOX_MAILBOX_1:
        return 1U;
    case EV_MAILBOX_LOSSY_RING_8:
        return 8U;
    case EV_MAILBOX_COALESCED_FLAG:
        return 1U;
    default:
        return 0U;
    }
}

bool ev_mailbox_kind_is_lossy(ev_mailbox_kind_t kind)
{
    return (kind == EV_MAILBOX_LOSSY_RING_8);
}

bool ev_mailbox_kind_is_coalescing(ev_mailbox_kind_t kind)
{
    return (kind == EV_MAILBOX_COALESCED_FLAG);
}

ev_result_t ev_mailbox_init(
    ev_mailbox_t *mailbox,
    ev_mailbox_kind_t kind,
    ev_msg_t *storage,
    size_t storage_count)
{
    if ((mailbox == NULL) || (storage == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_mailbox_kind_is_known(kind)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (storage_count != ev_mailbox_kind_capacity(kind)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (!ev_mailbox_capacity_is_power_of_two(storage_count)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    memset(mailbox, 0, sizeof(*mailbox));
    memset(storage, 0, sizeof(*storage) * storage_count);
    mailbox->kind = kind;
    mailbox->storage = storage;
    mailbox->storage_count = storage_count;
    mailbox->storage_mask = storage_count - 1U;
    return EV_OK;
}

ev_result_t ev_mailbox_reset(ev_mailbox_t *mailbox)
{
    size_t i;

    if ((mailbox == NULL) || (mailbox->storage == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    for (i = 0U; i < mailbox->storage_count; ++i) {
        (void)ev_mailbox_dispose_slot(mailbox, i);
    }
    mailbox->head = 0U;
    mailbox->tail = 0U;
    mailbox->count = 0U;
    memset(&mailbox->stats, 0, sizeof(mailbox->stats));
    return EV_OK;
}

ev_result_t ev_mailbox_push(ev_mailbox_t *mailbox, const ev_msg_t *msg)
{
    ev_result_t rc;

    if ((mailbox == NULL) || (msg == NULL) || (mailbox->storage == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_validate(msg);
    if (rc != EV_OK) {
        ++mailbox->stats.rejected;
        return rc;
    }

    switch (mailbox->kind) {
    case EV_MAILBOX_FIFO_8:
    case EV_MAILBOX_FIFO_16:
        if (mailbox->count >= mailbox->storage_count) {
            ++mailbox->stats.rejected;
            return EV_ERR_FULL;
        }
        rc = ev_mailbox_retain_for_queue(msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        rc = ev_mailbox_store_at(mailbox, mailbox->tail, msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        mailbox->tail = ev_mailbox_index_advance(mailbox, mailbox->tail);
        ++mailbox->count;
        ++mailbox->stats.posted;
        ev_mailbox_record_high_watermark(mailbox);
        return EV_OK;

    case EV_MAILBOX_MAILBOX_1:
        rc = ev_mailbox_retain_for_queue(msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        if (mailbox->count != 0U) {
            (void)ev_mailbox_dispose_slot(mailbox, 0U);
        }
        rc = ev_mailbox_store_at(mailbox, 0U, msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        if (mailbox->count == 0U) {
            mailbox->count = 1U;
            ++mailbox->stats.posted;
        } else {
            ++mailbox->stats.replaced;
        }
        ev_mailbox_record_high_watermark(mailbox);
        return EV_OK;

    case EV_MAILBOX_LOSSY_RING_8:
        rc = ev_mailbox_retain_for_queue(msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        if (mailbox->count >= mailbox->storage_count) {
            (void)ev_mailbox_dispose_slot(mailbox, mailbox->head);
            mailbox->head = ev_mailbox_index_advance(mailbox, mailbox->head);
            --mailbox->count;
            ++mailbox->stats.dropped;
        }
        rc = ev_mailbox_store_at(mailbox, mailbox->tail, msg);
        if (rc != EV_OK) {
            ++mailbox->stats.rejected;
            return rc;
        }
        mailbox->tail = ev_mailbox_index_advance(mailbox, mailbox->tail);
        ++mailbox->count;
        ++mailbox->stats.posted;
        ev_mailbox_record_high_watermark(mailbox);
        return EV_OK;

    case EV_MAILBOX_COALESCED_FLAG:
        if ((msg->storage != EV_MSG_STORAGE_NONE) || (msg->payload_size != 0U)) {
            ++mailbox->stats.rejected;
            return EV_ERR_CONTRACT;
        }
        if (mailbox->count == 0U) {
            rc = ev_mailbox_store_at(mailbox, 0U, msg);
            if (rc != EV_OK) {
                ++mailbox->stats.rejected;
                return rc;
            }
            mailbox->count = 1U;
            ++mailbox->stats.posted;
            ev_mailbox_record_high_watermark(mailbox);
            return EV_OK;
        }
        if (mailbox->storage[0].event_id != msg->event_id) {
            ++mailbox->stats.rejected;
            return EV_ERR_CONTRACT;
        }
        ++mailbox->stats.coalesced;
        return EV_OK;

    default:
        ++mailbox->stats.rejected;
        return EV_ERR_OUT_OF_RANGE;
    }
}

ev_result_t ev_mailbox_pop(ev_mailbox_t *mailbox, ev_msg_t *out)
{
    if ((mailbox == NULL) || (out == NULL) || (mailbox->storage == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (mailbox->count == 0U) {
        return EV_ERR_EMPTY;
    }

    switch (mailbox->kind) {
    case EV_MAILBOX_FIFO_8:
    case EV_MAILBOX_FIFO_16:
    case EV_MAILBOX_LOSSY_RING_8:
        *out = mailbox->storage[mailbox->head];
        ev_msg_reset(&mailbox->storage[mailbox->head]);
        mailbox->head = ev_mailbox_index_advance(mailbox, mailbox->head);
        --mailbox->count;
        ++mailbox->stats.popped;
        return EV_OK;

    case EV_MAILBOX_MAILBOX_1:
    case EV_MAILBOX_COALESCED_FLAG:
        *out = mailbox->storage[0U];
        ev_msg_reset(&mailbox->storage[0U]);
        mailbox->count = 0U;
        ++mailbox->stats.popped;
        return EV_OK;

    default:
        return EV_ERR_OUT_OF_RANGE;
    }
}

size_t ev_mailbox_count(const ev_mailbox_t *mailbox)
{
    return (mailbox != NULL) ? mailbox->count : 0U;
}

size_t ev_mailbox_capacity(const ev_mailbox_t *mailbox)
{
    return (mailbox != NULL) ? ev_mailbox_kind_capacity(mailbox->kind) : 0U;
}

bool ev_mailbox_is_empty(const ev_mailbox_t *mailbox)
{
    return ev_mailbox_count(mailbox) == 0U;
}

bool ev_mailbox_is_full(const ev_mailbox_t *mailbox)
{
    return (mailbox != NULL) && (mailbox->count >= ev_mailbox_capacity(mailbox));
}

const ev_mailbox_stats_t *ev_mailbox_stats(const ev_mailbox_t *mailbox)
{
    return (mailbox != NULL) ? &mailbox->stats : NULL;
}
