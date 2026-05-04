#include "ev/network_outbox.h"

#include <string.h>

#include "ev/compiler.h"

EV_STATIC_ASSERT((EV_NETWORK_OUTBOX_CAPACITY != 0U) && ((EV_NETWORK_OUTBOX_CAPACITY & EV_NETWORK_OUTBOX_MASK) == 0U), "EV_NETWORK_OUTBOX_CAPACITY must be a power of two");

void ev_network_outbox_init(ev_network_outbox_t *outbox)
{
    if (outbox != 0) {
        (void)memset(outbox, 0, sizeof(*outbox));
    }
}

static int ev_network_category_valid(ev_network_msg_category_t category)
{
    return ((category >= 0) && ((size_t)category < (size_t)EV_NETWORK_MSG_CATEGORY_COUNT));
}

static uint32_t ev_network_pending_u32(const ev_network_outbox_t *outbox)
{
    return (outbox != 0) ? (uint32_t)(outbox->write_seq - outbox->read_seq) : 0U;
}

size_t ev_network_outbox_pending(const ev_network_outbox_t *outbox)
{
    return (size_t)ev_network_pending_u32(outbox);
}

static void ev_network_copy_payload(ev_network_outbox_item_t *item, const uint8_t *payload, size_t size)
{
    item->size = (uint16_t)((size > EV_NETWORK_PAYLOAD_BYTES) ? EV_NETWORK_PAYLOAD_BYTES : size);
    if ((payload != 0) && (item->size > 0U)) {
        (void)memcpy(item->payload, payload, item->size);
    }
}

static void ev_network_drop_head(ev_network_outbox_t *outbox)
{
    uint32_t index = outbox->read_seq & EV_NETWORK_OUTBOX_MASK;
    ev_network_msg_category_t category = outbox->items[index].category;
    if (ev_network_category_valid(category) != 0 && outbox->queued_by_category[category] > 0U) {
        outbox->queued_by_category[category]--;
    }
    outbox->read_seq++;
    outbox->dropped++;
}

ev_result_t ev_network_outbox_push(ev_network_outbox_t *outbox, const ev_network_backpressure_policy_t *policy, ev_network_msg_category_t category, const uint8_t *payload, size_t size)
{
    size_t limit;
    uint32_t pending;
    uint32_t index;

    if ((outbox == 0) || (policy == 0) || (ev_network_category_valid(category) == 0)) {
        return EV_ERR_INVALID_ARG;
    }

    pending = ev_network_pending_u32(outbox);
    limit = policy->category_limit[category];
    if ((limit != 0U) && (outbox->queued_by_category[category] >= limit)) {
        if (policy->drop_oldest[category] != 0U && pending > 0U) {
            ev_network_drop_head(outbox);
            pending--;
        } else {
            outbox->rejected++;
            return EV_ERR_FULL;
        }
    }
    if (pending >= EV_NETWORK_OUTBOX_CAPACITY) {
        outbox->rejected++;
        return EV_ERR_FULL;
    }

    index = outbox->write_seq & EV_NETWORK_OUTBOX_MASK;
    outbox->items[index].category = category;
    ev_network_copy_payload(&outbox->items[index], payload, size);
    outbox->write_seq++;
    outbox->queued_by_category[category]++;
    outbox->accepted++;
    pending++;
    if (pending > outbox->high_water) {
        outbox->high_water = pending;
    }
    return EV_OK;
}

ev_result_t ev_network_outbox_pop(ev_network_outbox_t *outbox, ev_network_outbox_item_t *out_item)
{
    ev_network_msg_category_t category;
    uint32_t index;

    if ((outbox == 0) || (out_item == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_network_pending_u32(outbox) == 0U) {
        return EV_ERR_EMPTY;
    }
    index = outbox->read_seq & EV_NETWORK_OUTBOX_MASK;
    *out_item = outbox->items[index];
    category = out_item->category;
    if (ev_network_category_valid(category) != 0 && outbox->queued_by_category[category] > 0U) {
        outbox->queued_by_category[category]--;
    }
    outbox->read_seq++;
    return EV_OK;
}

ev_result_t ev_network_outbox_stats(const ev_network_outbox_t *outbox, ev_network_outbox_stats_t *out_stats)
{
    if ((outbox == 0) || (out_stats == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(out_stats, 0, sizeof(*out_stats));
    out_stats->pending = ev_network_outbox_pending(outbox);
    out_stats->high_water = outbox->high_water;
    out_stats->accepted = outbox->accepted;
    out_stats->rejected = outbox->rejected;
    out_stats->dropped = outbox->dropped;
    (void)memcpy(out_stats->queued_by_category, outbox->queued_by_category, sizeof(out_stats->queued_by_category));
    return EV_OK;
}
