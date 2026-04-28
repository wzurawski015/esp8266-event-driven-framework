#include "fake_net_port.h"

#include <string.h>

static size_t fake_net_pending_count(const fake_net_port_t *fake)
{
    if (fake == NULL) {
        return 0U;
    }
    return (size_t)(fake->write_seq - fake->read_seq);
}

static void fake_net_record_high_watermark(fake_net_port_t *fake)
{
    const size_t pending = fake_net_pending_count(fake);
    if ((fake != NULL) && (pending > fake->high_watermark)) {
        fake->high_watermark = (uint32_t)pending;
    }
}

static ev_result_t fake_net_payload_retain(void *ctx, const void *payload, size_t payload_size)
{
    fake_net_payload_slot_t *slot = (fake_net_payload_slot_t *)ctx;
    if ((slot == NULL) || (payload != (const void *)&slot->payload) ||
        (payload_size != sizeof(slot->payload)) || !slot->in_use || (slot->refcount == 0U)) {
        return EV_ERR_STATE;
    }
    ++slot->refcount;
    return EV_OK;
}

static void fake_net_payload_release(void *ctx, const void *payload, size_t payload_size)
{
    fake_net_payload_slot_t *slot = (fake_net_payload_slot_t *)ctx;
    fake_net_port_t *fake;

    if ((slot == NULL) || (payload != (const void *)&slot->payload) ||
        (payload_size != sizeof(slot->payload)) || !slot->in_use || (slot->refcount == 0U)) {
        return;
    }
    fake = (fake_net_port_t *)slot->owner;
    --slot->refcount;
    if (slot->refcount == 0U) {
        memset(&slot->payload, 0, sizeof(slot->payload));
        slot->in_use = false;
        if ((fake != NULL) && (fake->payload_slots_in_use > 0U)) {
            --fake->payload_slots_in_use;
        }
    }
    if (fake != NULL) {
        ++fake->payload_slot_release_count;
    }
}

static fake_net_payload_slot_t *fake_net_payload_acquire(fake_net_port_t *fake)
{
    size_t i;

    if (fake == NULL) {
        return NULL;
    }
    for (i = 0U; i < EV_NET_PAYLOAD_SLOT_COUNT; ++i) {
        fake_net_payload_slot_t *slot = &fake->payload_slots[i];
        if (!slot->in_use) {
            memset(&slot->payload, 0, sizeof(slot->payload));
            slot->owner = fake;
            slot->in_use = true;
            slot->refcount = 1U;
            ++slot->generation;
            if (slot->generation == 0U) {
                slot->generation = 1U;
            }
            ++fake->payload_slot_acquire_ok;
            ++fake->payload_slots_in_use;
            if (fake->payload_slots_in_use > fake->payload_slots_high_watermark) {
                fake->payload_slots_high_watermark = fake->payload_slots_in_use;
            }
            return slot;
        }
    }
    ++fake->payload_slot_acquire_failed;
    ++fake->dropped_no_payload_slot;
    return NULL;
}

static void fake_net_payload_make_lease(fake_net_payload_slot_t *slot, ev_net_payload_lease_t *out_lease)
{
    if (out_lease == NULL) {
        return;
    }
    memset(out_lease, 0, sizeof(*out_lease));
    if (slot == NULL) {
        return;
    }
    out_lease->data = &slot->payload;
    out_lease->size = sizeof(slot->payload);
    out_lease->retain_fn = fake_net_payload_retain;
    out_lease->release_fn = fake_net_payload_release;
    out_lease->lifecycle_ctx = slot;
}

static void fake_net_release_external_payload(const ev_net_payload_lease_t *payload)
{
    if ((payload != NULL) && (payload->release_fn != NULL) && (payload->data != NULL) && (payload->size > 0U)) {
        payload->release_fn(payload->lifecycle_ctx, payload->data, payload->size);
    }
}

static void fake_net_record_event_kind(fake_net_port_t *fake, const ev_net_ingress_event_t *event)
{
    if ((fake == NULL) || (event == NULL)) {
        return;
    }

    switch (event->kind) {
    case EV_NET_EVENT_WIFI_UP:
        ++fake->wifi_up_events;
        break;
    case EV_NET_EVENT_WIFI_DOWN:
        ++fake->wifi_down_events;
        break;
    case EV_NET_EVENT_MQTT_UP:
        ++fake->mqtt_up_events;
        break;
    case EV_NET_EVENT_MQTT_DOWN:
        ++fake->mqtt_down_events;
        break;
    case EV_NET_EVENT_MQTT_MSG_RX:
        ++fake->mqtt_rx_events;
        if (event->payload_storage == EV_NET_PAYLOAD_LEASE) {
            const ev_net_mqtt_rx_payload_t *rx = (const ev_net_mqtt_rx_payload_t *)event->external_payload.data;
            ++fake->mqtt_rx_slot_events;
            if (rx != NULL) {
                fake->mqtt_rx_bytes += rx->payload_len;
            }
        } else {
            ++fake->mqtt_rx_inline_events;
            fake->mqtt_rx_bytes += event->payload_len;
        }
        break;
    default:
        break;
    }
}

static ev_result_t fake_net_init_fn(void *ctx)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->init_calls;
    return EV_OK;
}

static ev_result_t fake_net_start_fn(void *ctx)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->start_calls;
    return EV_OK;
}

static ev_result_t fake_net_stop_fn(void *ctx)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->stop_calls;
    return EV_OK;
}

static void fake_net_record_publish_view(fake_net_port_t *fake, const ev_net_mqtt_publish_view_t *view)
{
    if ((fake == NULL) || (view == NULL)) {
        return;
    }
    memset(fake->last_publish_topic, 0, sizeof(fake->last_publish_topic));
    memset(fake->last_publish_payload, 0, sizeof(fake->last_publish_payload));
    fake->last_publish_topic_len = view->topic_len;
    fake->last_publish_payload_len = view->payload_len;
    fake->last_publish_qos = view->qos;
    fake->last_publish_retain = view->retain;
    if ((view->topic != NULL) && (view->topic_len <= sizeof(fake->last_publish_topic))) {
        memcpy(fake->last_publish_topic, view->topic, view->topic_len);
    }
    if ((view->payload != NULL) && (view->payload_len <= sizeof(fake->last_publish_payload))) {
        memcpy(fake->last_publish_payload, view->payload, view->payload_len);
    }
}

static ev_result_t fake_net_publish_mqtt_fn(void *ctx, const ev_net_mqtt_publish_cmd_t *cmd)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    if ((fake == NULL) || (cmd == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->publish_mqtt_calls;
    {
        ev_net_mqtt_publish_view_t view;
        memset(&view, 0, sizeof(view));
        view.topic = cmd->topic;
        view.topic_len = cmd->topic_len;
        view.payload = cmd->payload;
        view.payload_len = cmd->payload_len;
        view.qos = cmd->qos;
        view.retain = cmd->retain;
        fake_net_record_publish_view(fake, &view);
    }
    if (fake->next_publish_result == EV_OK) {
        ++fake->publish_mqtt_ok;
    } else {
        ++fake->publish_mqtt_failed;
        if ((fake->next_publish_result == EV_ERR_STATE) ||
            (fake->next_publish_result == EV_ERR_UNSUPPORTED)) {
            ++fake->tx_rejected_state;
        }
    }
    return fake->next_publish_result;
}

static ev_result_t fake_net_publish_mqtt_view_fn(void *ctx, const ev_net_mqtt_publish_view_t *view)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    if ((fake == NULL) || (view == NULL) || (view->topic == NULL) ||
        ((view->payload_len > 0U) && (view->payload == NULL))) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->publish_mqtt_view_calls;
    fake_net_record_publish_view(fake, view);
    if (fake->next_publish_result == EV_OK) {
        ++fake->publish_mqtt_ok;
    } else {
        ++fake->publish_mqtt_failed;
        if ((fake->next_publish_result == EV_ERR_STATE) ||
            (fake->next_publish_result == EV_ERR_UNSUPPORTED)) {
            ++fake->tx_rejected_state;
        }
    }
    return fake->next_publish_result;
}

static ev_result_t fake_net_poll_ingress_fn(void *ctx, ev_net_ingress_event_t *out_event)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;
    uint32_t index;

    if ((fake == NULL) || (out_event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->poll_calls;
    if (fake_net_pending_count(fake) == 0U) {
        return EV_ERR_EMPTY;
    }

    index = fake->read_seq & EV_NET_INGRESS_RING_MASK;
    *out_event = fake->ring[index];
    ++fake->read_seq;
    return EV_OK;
}

static ev_result_t fake_net_get_stats_fn(void *ctx, ev_net_stats_t *out_stats)
{
    fake_net_port_t *fake = (fake_net_port_t *)ctx;

    if ((fake == NULL) || (out_stats == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->get_stats_calls;
    ++fake->state_snapshots;
    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->write_seq = fake->write_seq;
    out_stats->read_seq = fake->read_seq;
    out_stats->pending_events = (uint32_t)fake_net_pending_count(fake);
    out_stats->dropped_events = fake->dropped_events;
    out_stats->dropped_oversize = fake->dropped_oversize;
    out_stats->dropped_no_payload_slot = fake->dropped_no_payload_slot;
    out_stats->high_watermark = fake->high_watermark;
    out_stats->wifi_up_events = fake->wifi_up_events;
    out_stats->wifi_down_events = fake->wifi_down_events;
    out_stats->reconnect_attempts = fake->reconnect_attempts;
    out_stats->reconnect_suppressed = fake->reconnect_suppressed;
    out_stats->duplicate_wifi_down_suppressed = fake->duplicate_wifi_down_suppressed;
    out_stats->duplicate_wifi_up_suppressed = fake->duplicate_wifi_up_suppressed;
    out_stats->event_loop_init_failures = fake->event_loop_init_failures;
    out_stats->event_loop_already_initialized = fake->event_loop_already_initialized;
    out_stats->state_snapshots = fake->state_snapshots;
    out_stats->callback_state_updates = fake->callback_state_updates;
    out_stats->mqtt_disabled = fake->mqtt_disabled;
    out_stats->mqtt_connect_attempts = fake->mqtt_connect_attempts;
    out_stats->mqtt_up_events = fake->mqtt_up_events;
    out_stats->mqtt_down_events = fake->mqtt_down_events;
    out_stats->mqtt_rx_events = fake->mqtt_rx_events;
    out_stats->mqtt_rx_bytes = fake->mqtt_rx_bytes;
    out_stats->mqtt_rx_inline_events = fake->mqtt_rx_inline_events;
    out_stats->mqtt_rx_slot_events = fake->mqtt_rx_slot_events;
    out_stats->payload_slots_total = EV_NET_PAYLOAD_SLOT_COUNT;
    out_stats->payload_slots_in_use = fake->payload_slots_in_use;
    out_stats->payload_slots_high_watermark = fake->payload_slots_high_watermark;
    out_stats->payload_slot_acquire_ok = fake->payload_slot_acquire_ok;
    out_stats->payload_slot_acquire_failed = fake->payload_slot_acquire_failed;
    out_stats->payload_slot_release_count = fake->payload_slot_release_count;
    out_stats->tx_attempts = fake->publish_mqtt_calls + fake->publish_mqtt_view_calls;
    out_stats->tx_ok = fake->publish_mqtt_ok;
    out_stats->tx_failed = fake->publish_mqtt_failed;
    out_stats->tx_rejected_state = fake->tx_rejected_state;
    out_stats->tx_rejected_oversize = fake->tx_rejected_oversize;
    return EV_OK;
}

void fake_net_port_init(fake_net_port_t *fake)
{
    size_t i;
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
        for (i = 0U; i < EV_NET_PAYLOAD_SLOT_COUNT; ++i) {
            fake->payload_slots[i].owner = fake;
        }
        fake->next_publish_result = EV_OK;
    }
}

void fake_net_port_bind(ev_net_port_t *out_port, fake_net_port_t *fake)
{
    if (out_port != NULL) {
        memset(out_port, 0, sizeof(*out_port));
        out_port->ctx = fake;
        out_port->init = fake_net_init_fn;
        out_port->start = fake_net_start_fn;
        out_port->stop = fake_net_stop_fn;
        out_port->publish_mqtt = fake_net_publish_mqtt_fn;
        out_port->publish_mqtt_view = fake_net_publish_mqtt_view_fn;
        out_port->poll_ingress = fake_net_poll_ingress_fn;
        out_port->get_stats = fake_net_get_stats_fn;
    }
}

ev_result_t fake_net_port_callback_push(fake_net_port_t *fake, const ev_net_ingress_event_t *event)
{
    uint32_t index;

    if ((fake == NULL) || (event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->callback_push_calls;
    if (fake_net_pending_count(fake) >= EV_NET_INGRESS_RING_CAPACITY) {
        ++fake->dropped_events;
        return EV_ERR_FULL;
    }

    index = fake->write_seq & EV_NET_INGRESS_RING_MASK;
    fake->ring[index] = *event;
    ++fake->write_seq;
    fake_net_record_event_kind(fake, event);
    fake_net_record_high_watermark(fake);
    return EV_OK;
}

ev_result_t fake_net_port_callback_wifi_up(fake_net_port_t *fake)
{
    ev_net_ingress_event_t event;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (fake->wifi_connected) {
        ++fake->duplicate_wifi_up_suppressed;
        return EV_ERR_STATE;
    }

    fake->wifi_connected = true;
    ++fake->callback_state_updates;
    memset(&event, 0, sizeof(event));
    event.kind = EV_NET_EVENT_WIFI_UP;
    return fake_net_port_callback_push(fake, &event);
}

ev_result_t fake_net_port_callback_wifi_down(fake_net_port_t *fake)
{
    ev_net_ingress_event_t event;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!fake->wifi_connected) {
        ++fake->duplicate_wifi_down_suppressed;
        ++fake->reconnect_suppressed;
        return EV_ERR_STATE;
    }

    fake->wifi_connected = false;
    if (fake->mqtt_connected) {
        fake->mqtt_connected = false;
    }
    ++fake->callback_state_updates;
    ++fake->reconnect_attempts;
    memset(&event, 0, sizeof(event));
    event.kind = EV_NET_EVENT_WIFI_DOWN;
    return fake_net_port_callback_push(fake, &event);
}

ev_result_t fake_net_port_callback_push_mqtt(fake_net_port_t *fake,
                                             const char *topic,
                                             size_t topic_len,
                                             const uint8_t *payload,
                                             size_t payload_len)
{
    ev_net_ingress_event_t event;
    fake_net_payload_slot_t *slot;

    if ((fake == NULL) || (topic == NULL) || ((payload_len > 0U) && (payload == NULL))) {
        return EV_ERR_INVALID_ARG;
    }
    if ((topic_len > EV_NET_MAX_TOPIC_STORAGE_BYTES) || (payload_len > EV_NET_MAX_PAYLOAD_STORAGE_BYTES)) {
        ++fake->dropped_oversize;
        return EV_ERR_OUT_OF_RANGE;
    }

    memset(&event, 0, sizeof(event));
    event.kind = EV_NET_EVENT_MQTT_MSG_RX;
    if ((topic_len <= EV_NET_MAX_TOPIC_BYTES) && (payload_len <= EV_NET_MAX_INLINE_PAYLOAD_BYTES)) {
        event.payload_storage = EV_NET_PAYLOAD_INLINE;
        event.topic_len = (uint8_t)topic_len;
        event.payload_len = (uint8_t)payload_len;
        if (topic_len > 0U) {
            memcpy(event.topic, topic, topic_len);
        }
        if (payload_len > 0U) {
            memcpy(event.payload, payload, payload_len);
        }
        return fake_net_port_callback_push(fake, &event);
    }

    slot = fake_net_payload_acquire(fake);
    if (slot == NULL) {
        return EV_ERR_FULL;
    }
    slot->payload.topic_len = (uint8_t)topic_len;
    slot->payload.payload_len = (uint8_t)payload_len;
    slot->payload.qos = 0U;
    slot->payload.retain = 0U;
    if (topic_len > 0U) {
        memcpy(slot->payload.topic, topic, topic_len);
    }
    if (payload_len > 0U) {
        memcpy(slot->payload.payload, payload, payload_len);
    }

    event.payload_storage = EV_NET_PAYLOAD_LEASE;
    event.topic_len = slot->payload.topic_len;
    event.payload_len = slot->payload.payload_len;
    fake_net_payload_make_lease(slot, &event.external_payload);
    if (fake_net_port_callback_push(fake, &event) != EV_OK) {
        fake_net_release_external_payload(&event.external_payload);
        return EV_ERR_FULL;
    }
    return EV_OK;
}

size_t fake_net_port_pending(const fake_net_port_t *fake)
{
    return fake_net_pending_count(fake);
}
