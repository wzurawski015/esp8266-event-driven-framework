#include "ev/network_outbox.h"

#include <string.h>

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

static void ev_network_copy_payload(ev_network_outbox_item_t *item, const uint8_t *payload, size_t size)
{
    item->size = (uint16_t)((size > EV_NETWORK_PAYLOAD_BYTES) ? EV_NETWORK_PAYLOAD_BYTES : size);
    if ((payload != 0) && (item->size > 0U)) {
        (void)memcpy(item->payload, payload, item->size);
    }
}

static void ev_network_drop_head(ev_network_outbox_t *outbox)
{
    ev_network_msg_category_t category = outbox->items[outbox->head].category;
    if (ev_network_category_valid(category) != 0 && outbox->queued_by_category[category] > 0U) {
        outbox->queued_by_category[category]--;
    }
    outbox->head = (outbox->head + 1U) % EV_NETWORK_OUTBOX_CAPACITY;
    outbox->count--;
    outbox->dropped++;
}

ev_result_t ev_network_outbox_push(ev_network_outbox_t *outbox, const ev_network_backpressure_policy_t *policy, ev_network_msg_category_t category, const uint8_t *payload, size_t size)
{
    size_t limit;
    size_t index;

    if ((outbox == 0) || (policy == 0) || (ev_network_category_valid(category) == 0)) {
        return EV_ERR_INVALID_ARG;
    }

    limit = policy->category_limit[category];
    if ((limit != 0U) && (outbox->queued_by_category[category] >= limit)) {
        if (policy->drop_oldest[category] != 0U && outbox->count > 0U) {
            ev_network_drop_head(outbox);
        } else {
            outbox->rejected++;
            return EV_ERR_FULL;
        }
    }
    if (outbox->count >= EV_NETWORK_OUTBOX_CAPACITY) {
        outbox->rejected++;
        return EV_ERR_FULL;
    }

    index = (outbox->head + outbox->count) % EV_NETWORK_OUTBOX_CAPACITY;
    outbox->items[index].category = category;
    ev_network_copy_payload(&outbox->items[index], payload, size);
    outbox->count++;
    outbox->queued_by_category[category]++;
    outbox->accepted++;
    return EV_OK;
}

ev_result_t ev_network_outbox_pop(ev_network_outbox_t *outbox, ev_network_outbox_item_t *out_item)
{
    ev_network_msg_category_t category;

    if ((outbox == 0) || (out_item == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    if (outbox->count == 0U) {
        return EV_ERR_EMPTY;
    }
    *out_item = outbox->items[outbox->head];
    category = out_item->category;
    if (ev_network_category_valid(category) != 0 && outbox->queued_by_category[category] > 0U) {
        outbox->queued_by_category[category]--;
    }
    outbox->head = (outbox->head + 1U) % EV_NETWORK_OUTBOX_CAPACITY;
    outbox->count--;
    return EV_OK;
}
