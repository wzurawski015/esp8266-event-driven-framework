#include "ev/esp8266_port_adapters.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_err.h"
#include "esp_event_loop.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"

#ifndef EV_ESP8266_NET_ENABLE_MQTT
#define EV_ESP8266_NET_ENABLE_MQTT 0
#endif

#if EV_ESP8266_NET_ENABLE_MQTT
#include "mqtt_client.h"
#endif

#define EV_ESP8266_NET_WIFI_AUTH_OPEN 0U
#define EV_ESP8266_NET_WIFI_AUTH_WPA2_PSK 1U
#define EV_ESP8266_NET_RECONNECT_MIN_INTERVAL_MS 1000U
#define EV_ESP8266_NET_RECONNECT_MIN_INTERVAL_US \
    ((int64_t)EV_ESP8266_NET_RECONNECT_MIN_INTERVAL_MS * 1000LL)

/*
 * ESP8266 physical network adapter for the portable HSHA Network Airlock.
 *
 * SDK callbacks never call core, actors, mailboxes, or ev_publish(). They copy
 * bounded event metadata into this static ingress ring. The synchronous app poll
 * loop later drains the ring through ev_net_port_t::poll_ingress and publishes
 * EV_NET_* events into the actor graph.
 *
 * Shared callback/poll state is protected by short critical sections. No SDK
 * function is called while that critical section is held.
 */

typedef struct ev_esp8266_net_payload_slot {
    bool in_use;
    uint32_t refcount;
    uint32_t generation;
    ev_net_mqtt_rx_payload_t payload;
} ev_esp8266_net_payload_slot_t;

typedef struct ev_esp8266_net_ctx {
    ev_net_ingress_event_t ring[EV_NET_INGRESS_RING_CAPACITY];
    ev_esp8266_net_payload_slot_t payload_slots[EV_NET_PAYLOAD_SLOT_COUNT];
    uint32_t write_seq;
    uint32_t read_seq;
    uint32_t high_watermark;
    uint32_t dropped_events;
    uint32_t dropped_oversize;
    uint32_t dropped_no_payload_slot;
    uint32_t payload_slots_in_use;
    uint32_t payload_slots_high_watermark;
    uint32_t payload_slot_acquire_ok;
    uint32_t payload_slot_acquire_failed;
    uint32_t payload_slot_release_count;
    uint32_t mqtt_rx_bytes;
    uint32_t mqtt_rx_inline_events;
    uint32_t mqtt_rx_slot_events;
    uint32_t wifi_up_events;
    uint32_t wifi_down_events;
    uint32_t reconnect_attempts;
    uint32_t reconnect_suppressed;
    uint32_t duplicate_wifi_down_suppressed;
    uint32_t duplicate_wifi_up_suppressed;
    uint32_t event_loop_init_failures;
    uint32_t event_loop_already_initialized;
    uint32_t state_snapshots;
    uint32_t callback_state_updates;
    uint32_t mqtt_disabled;
    uint32_t mqtt_connect_attempts;
    uint32_t mqtt_up_events;
    uint32_t mqtt_down_events;
    uint32_t mqtt_rx_events;
    uint32_t tx_attempts;
    uint32_t tx_ok;
    uint32_t tx_failed;
    uint32_t tx_rejected_state;
    uint32_t tx_rejected_oversize;
    int64_t last_reconnect_attempt_us;
    bool initialized;
    bool wifi_started;
    bool wifi_connected;
    bool mqtt_supported;
    bool mqtt_started;
    bool mqtt_connected;
    bool event_loop_initialized;
    bool event_loop_owned;
    ev_esp8266_net_config_t cfg;
#if EV_ESP8266_NET_ENABLE_MQTT
    esp_mqtt_client_handle_t mqtt_client;
#endif
} ev_esp8266_net_ctx_t;

typedef struct ev_esp8266_net_state_snapshot {
    bool initialized;
    bool wifi_started;
    bool wifi_connected;
    bool mqtt_supported;
    bool mqtt_started;
    bool mqtt_connected;
#if EV_ESP8266_NET_ENABLE_MQTT
    esp_mqtt_client_handle_t mqtt_client;
#endif
} ev_esp8266_net_state_snapshot_t;

typedef struct ev_esp8266_net_wifi_down_actions {
    bool push_wifi_down;
    bool push_mqtt_down;
    bool reconnect;
} ev_esp8266_net_wifi_down_actions_t;

static ev_esp8266_net_ctx_t g_ev_esp8266_net_ctx;

static void ev_esp8266_net_lock(void)
{
    portENTER_CRITICAL();
}

static void ev_esp8266_net_unlock(void)
{
    portEXIT_CRITICAL();
}

static uint32_t ev_esp8266_net_pending_unsafe(const ev_esp8266_net_ctx_t *net)
{
    return (net != NULL) ? (net->write_seq - net->read_seq) : 0U;
}

static void ev_esp8266_net_record_high_watermark_unsafe(ev_esp8266_net_ctx_t *net)
{
    const uint32_t pending = ev_esp8266_net_pending_unsafe(net);

    if ((net != NULL) && (pending > net->high_watermark)) {
        net->high_watermark = pending;
    }
}

static bool ev_esp8266_net_string_is_empty(const char *value)
{
    return (value == NULL) || (value[0] == '\0');
}

static void ev_esp8266_net_increment_counter(uint32_t *counter)
{
    if (counter == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    ++(*counter);
    ev_esp8266_net_unlock();
}

static ev_result_t ev_esp8266_net_payload_retain(void *ctx, const void *payload, size_t payload_size)
{
    ev_esp8266_net_payload_slot_t *slot = (ev_esp8266_net_payload_slot_t *)ctx;

    if ((slot == NULL) || (payload != (const void *)&slot->payload) ||
        (payload_size != sizeof(slot->payload))) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    if (!slot->in_use || (slot->refcount == 0U)) {
        ev_esp8266_net_unlock();
        return EV_ERR_STATE;
    }
    ++slot->refcount;
    ev_esp8266_net_unlock();
    return EV_OK;
}

static void ev_esp8266_net_payload_release(void *ctx, const void *payload, size_t payload_size)
{
    ev_esp8266_net_payload_slot_t *slot = (ev_esp8266_net_payload_slot_t *)ctx;

    if ((slot == NULL) || (payload != (const void *)&slot->payload) ||
        (payload_size != sizeof(slot->payload))) {
        return;
    }

    ev_esp8266_net_lock();
    if (slot->in_use && (slot->refcount > 0U)) {
        --slot->refcount;
        if (slot->refcount == 0U) {
            memset(&slot->payload, 0, sizeof(slot->payload));
            slot->in_use = false;
            if (g_ev_esp8266_net_ctx.payload_slots_in_use > 0U) {
                --g_ev_esp8266_net_ctx.payload_slots_in_use;
            }
        }
        ++g_ev_esp8266_net_ctx.payload_slot_release_count;
    }
    ev_esp8266_net_unlock();
}

static ev_esp8266_net_payload_slot_t *ev_esp8266_net_payload_acquire(ev_esp8266_net_ctx_t *net)
{
    size_t i;
    ev_esp8266_net_payload_slot_t *slot = NULL;

    if (net == NULL) {
        return NULL;
    }

    ev_esp8266_net_lock();
    for (i = 0U; i < EV_NET_PAYLOAD_SLOT_COUNT; ++i) {
        if (!net->payload_slots[i].in_use) {
            slot = &net->payload_slots[i];
            memset(&slot->payload, 0, sizeof(slot->payload));
            slot->in_use = true;
            slot->refcount = 1U;
            ++slot->generation;
            if (slot->generation == 0U) {
                slot->generation = 1U;
            }
            ++net->payload_slot_acquire_ok;
            ++net->payload_slots_in_use;
            if (net->payload_slots_in_use > net->payload_slots_high_watermark) {
                net->payload_slots_high_watermark = net->payload_slots_in_use;
            }
            break;
        }
    }
    if (slot == NULL) {
        ++net->payload_slot_acquire_failed;
        ++net->dropped_no_payload_slot;
    }
    ev_esp8266_net_unlock();
    return slot;
}

static void ev_esp8266_net_payload_make_lease(ev_esp8266_net_payload_slot_t *slot,
                                              ev_net_payload_lease_t *out_lease)
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
    out_lease->retain_fn = ev_esp8266_net_payload_retain;
    out_lease->release_fn = ev_esp8266_net_payload_release;
    out_lease->lifecycle_ctx = slot;
}


static ev_esp8266_net_state_snapshot_t ev_esp8266_net_snapshot_state(ev_esp8266_net_ctx_t *net)
{
    ev_esp8266_net_state_snapshot_t snapshot;

    memset(&snapshot, 0, sizeof(snapshot));
    if (net == NULL) {
        return snapshot;
    }

    ev_esp8266_net_lock();
    snapshot.initialized = net->initialized;
    snapshot.wifi_started = net->wifi_started;
    snapshot.wifi_connected = net->wifi_connected;
    snapshot.mqtt_supported = net->mqtt_supported;
    snapshot.mqtt_started = net->mqtt_started;
    snapshot.mqtt_connected = net->mqtt_connected;
#if EV_ESP8266_NET_ENABLE_MQTT
    snapshot.mqtt_client = net->mqtt_client;
#endif
    ++net->state_snapshots;
    ev_esp8266_net_unlock();
    return snapshot;
}

static void ev_esp8266_net_set_initialized(ev_esp8266_net_ctx_t *net, bool initialized)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    net->initialized = initialized;
    ++net->callback_state_updates;
    ev_esp8266_net_unlock();
}

static void ev_esp8266_net_set_wifi_started(ev_esp8266_net_ctx_t *net, bool started)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    net->wifi_started = started;
    ++net->callback_state_updates;
    ev_esp8266_net_unlock();
}

#if EV_ESP8266_NET_ENABLE_MQTT
static void ev_esp8266_net_set_mqtt_started(ev_esp8266_net_ctx_t *net, bool started)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    net->mqtt_started = started;
    ++net->callback_state_updates;
    ev_esp8266_net_unlock();
}

#endif

static void ev_esp8266_net_set_mqtt_supported(ev_esp8266_net_ctx_t *net, bool supported)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    net->mqtt_supported = supported;
    ++net->callback_state_updates;
    ev_esp8266_net_unlock();
}

static void ev_esp8266_net_note_event_loop_failure(ev_esp8266_net_ctx_t *net)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    ++net->event_loop_init_failures;
    ev_esp8266_net_unlock();
}

static void ev_esp8266_net_mark_event_loop_owned(ev_esp8266_net_ctx_t *net)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    net->event_loop_initialized = true;
    net->event_loop_owned = true;
    ++net->callback_state_updates;
    ev_esp8266_net_unlock();
}

static bool ev_esp8266_net_note_wifi_up(ev_esp8266_net_ctx_t *net)
{
    bool should_push = false;

    if (net == NULL) {
        return false;
    }

    ev_esp8266_net_lock();
    if (!net->wifi_connected) {
        net->wifi_connected = true;
        ++net->wifi_up_events;
        ++net->callback_state_updates;
        should_push = true;
    } else {
        ++net->duplicate_wifi_up_suppressed;
    }
    ev_esp8266_net_unlock();
    return should_push;
}

static ev_esp8266_net_wifi_down_actions_t ev_esp8266_net_note_wifi_down(ev_esp8266_net_ctx_t *net,
                                                                        int64_t now_us)
{
    ev_esp8266_net_wifi_down_actions_t actions;

    memset(&actions, 0, sizeof(actions));
    if (net == NULL) {
        return actions;
    }

    ev_esp8266_net_lock();
    if (net->wifi_connected) {
        net->wifi_connected = false;
        ++net->wifi_down_events;
        ++net->callback_state_updates;
        actions.push_wifi_down = true;
    } else {
        ++net->duplicate_wifi_down_suppressed;
    }

    if (net->mqtt_connected) {
        net->mqtt_connected = false;
        ++net->mqtt_down_events;
        ++net->callback_state_updates;
        actions.push_mqtt_down = true;
    }

    if ((net->last_reconnect_attempt_us == 0LL) ||
        ((now_us - net->last_reconnect_attempt_us) >= EV_ESP8266_NET_RECONNECT_MIN_INTERVAL_US)) {
        net->last_reconnect_attempt_us = now_us;
        ++net->reconnect_attempts;
        actions.reconnect = true;
    } else {
        ++net->reconnect_suppressed;
    }
    ev_esp8266_net_unlock();
    return actions;
}

#if EV_ESP8266_NET_ENABLE_MQTT
static bool ev_esp8266_net_note_mqtt_up(ev_esp8266_net_ctx_t *net)
{
    bool should_push = false;

    if (net == NULL) {
        return false;
    }

    ev_esp8266_net_lock();
    if (!net->mqtt_connected) {
        net->mqtt_connected = true;
        ++net->mqtt_up_events;
        ++net->callback_state_updates;
        should_push = true;
    }
    ev_esp8266_net_unlock();
    return should_push;
}

static bool ev_esp8266_net_note_mqtt_down(ev_esp8266_net_ctx_t *net)
{
    bool should_push = false;

    if (net == NULL) {
        return false;
    }

    ev_esp8266_net_lock();
    if (net->mqtt_connected) {
        net->mqtt_connected = false;
        ++net->mqtt_down_events;
        ++net->callback_state_updates;
        should_push = true;
    }
    ev_esp8266_net_unlock();
    return should_push;
}

#endif

static ev_result_t ev_esp8266_net_push_event(ev_esp8266_net_ctx_t *net, const ev_net_ingress_event_t *event)
{
    uint32_t index;

    if ((net == NULL) || (event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    if (ev_esp8266_net_pending_unsafe(net) >= EV_NET_INGRESS_RING_CAPACITY) {
        ++net->dropped_events;
        ev_esp8266_net_unlock();
        return EV_ERR_FULL;
    }

    index = net->write_seq & EV_NET_INGRESS_RING_MASK;
    net->ring[index] = *event;
    ++net->write_seq;
    ev_esp8266_net_record_high_watermark_unsafe(net);
    ev_esp8266_net_unlock();
    return EV_OK;
}

static void ev_esp8266_net_push_kind(ev_esp8266_net_ctx_t *net, ev_net_event_kind_t kind)
{
    ev_net_ingress_event_t event;

    memset(&event, 0, sizeof(event));
    event.kind = kind;
    (void)ev_esp8266_net_push_event(net, &event);
}

#if EV_ESP8266_NET_ENABLE_MQTT
static void ev_esp8266_net_record_mqtt_rx_success(ev_esp8266_net_ctx_t *net,
                                                  uint32_t payload_len,
                                                  bool leased)
{
    if (net == NULL) {
        return;
    }

    ev_esp8266_net_lock();
    ++net->mqtt_rx_events;
    net->mqtt_rx_bytes += payload_len;
    if (leased) {
        ++net->mqtt_rx_slot_events;
    } else {
        ++net->mqtt_rx_inline_events;
    }
    ev_esp8266_net_unlock();
}

static void ev_esp8266_net_push_mqtt_data(ev_esp8266_net_ctx_t *net,
                                          const char *topic,
                                          int topic_len,
                                          const char *payload,
                                          int payload_len)
{
    ev_net_ingress_event_t event;
    ev_esp8266_net_payload_slot_t *slot;
    ev_result_t rc;

    if ((net == NULL) || (topic == NULL) || (topic_len < 0) || (payload_len < 0) ||
        ((payload_len > 0) && (payload == NULL))) {
        return;
    }

    if (((size_t)topic_len <= EV_NET_MAX_TOPIC_BYTES) &&
        ((size_t)payload_len <= EV_NET_MAX_INLINE_PAYLOAD_BYTES)) {
        memset(&event, 0, sizeof(event));
        event.kind = EV_NET_EVENT_MQTT_MSG_RX;
        event.payload_storage = EV_NET_PAYLOAD_INLINE;
        event.topic_len = (uint8_t)topic_len;
        event.payload_len = (uint8_t)payload_len;
        if (topic_len > 0) {
            memcpy(event.topic, topic, (size_t)topic_len);
        }
        if (payload_len > 0) {
            memcpy(event.payload, payload, (size_t)payload_len);
        }
        rc = ev_esp8266_net_push_event(net, &event);
        if (rc == EV_OK) {
            ev_esp8266_net_record_mqtt_rx_success(net, (uint32_t)payload_len, false);
        }
        return;
    }

    if (((size_t)topic_len > EV_NET_MAX_TOPIC_STORAGE_BYTES) ||
        ((size_t)payload_len > EV_NET_MAX_PAYLOAD_STORAGE_BYTES)) {
        ev_esp8266_net_increment_counter(&net->dropped_oversize);
        return;
    }

    slot = ev_esp8266_net_payload_acquire(net);
    if (slot == NULL) {
        return;
    }

    slot->payload.topic_len = (uint8_t)topic_len;
    slot->payload.payload_len = (uint8_t)payload_len;
    if (topic_len > 0) {
        memcpy(slot->payload.topic, topic, (size_t)topic_len);
    }
    if (payload_len > 0) {
        memcpy(slot->payload.payload, payload, (size_t)payload_len);
    }

    memset(&event, 0, sizeof(event));
    event.kind = EV_NET_EVENT_MQTT_MSG_RX;
    event.payload_storage = EV_NET_PAYLOAD_LEASE;
    event.topic_len = slot->payload.topic_len;
    event.payload_len = slot->payload.payload_len;
    ev_esp8266_net_payload_make_lease(slot, &event.external_payload);

    rc = ev_esp8266_net_push_event(net, &event);
    if (rc != EV_OK) {
        ev_esp8266_net_payload_release(slot, &slot->payload, sizeof(slot->payload));
        return;
    }

    ev_esp8266_net_record_mqtt_rx_success(net, (uint32_t)payload_len, true);
}

#endif

static wifi_auth_mode_t ev_esp8266_net_auth_mode(uint32_t auth_mode)
{
    if (auth_mode == EV_ESP8266_NET_WIFI_AUTH_WPA2_PSK) {
        return WIFI_AUTH_WPA2_PSK;
    }
    return WIFI_AUTH_OPEN;
}

static esp_err_t ev_esp8266_wifi_event_handler(void *ctx, system_event_t *event)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;

    if ((net == NULL) || (event == NULL)) {
        return ESP_OK;
    }

    switch (event->event_id) {
    case SYSTEM_EVENT_STA_GOT_IP:
        if (ev_esp8266_net_note_wifi_up(net)) {
            ev_esp8266_net_push_kind(net, EV_NET_EVENT_WIFI_UP);
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED: {
        const int64_t now_us = esp_timer_get_time();
        const ev_esp8266_net_wifi_down_actions_t actions = ev_esp8266_net_note_wifi_down(net, now_us);
        if (actions.push_mqtt_down) {
            ev_esp8266_net_push_kind(net, EV_NET_EVENT_MQTT_DOWN);
        }
        if (actions.push_wifi_down) {
            ev_esp8266_net_push_kind(net, EV_NET_EVENT_WIFI_DOWN);
        }
        if (actions.reconnect) {
            (void)esp_wifi_connect();
        }
        break;
    }
    default:
        break;
    }

    return ESP_OK;
}

#if EV_ESP8266_NET_ENABLE_MQTT
static esp_err_t ev_esp8266_mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    ev_esp8266_net_ctx_t *net;

    if (event == NULL) {
        return ESP_OK;
    }

    net = (ev_esp8266_net_ctx_t *)event->user_context;
    if (net == NULL) {
        return ESP_OK;
    }

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        if (ev_esp8266_net_note_mqtt_up(net)) {
            ev_esp8266_net_push_kind(net, EV_NET_EVENT_MQTT_UP);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
    case MQTT_EVENT_ERROR:
        if (ev_esp8266_net_note_mqtt_down(net)) {
            ev_esp8266_net_push_kind(net, EV_NET_EVENT_MQTT_DOWN);
        }
        break;
    case MQTT_EVENT_DATA:
        ev_esp8266_net_push_mqtt_data(net,
                                      event->topic,
                                      event->topic_len,
                                      event->data,
                                      event->data_len);
        break;
    default:
        break;
    }

    return ESP_OK;
}

#endif

static ev_result_t ev_esp8266_net_config_is_valid(const ev_esp8266_net_config_t *cfg)
{
    if ((cfg == NULL) || ev_esp8266_net_string_is_empty(cfg->wifi_ssid) || (cfg->wifi_password == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

static ev_result_t ev_esp8266_net_init_fn(void *ctx)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;
    wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    wifi_config_t wifi_cfg;
    ev_esp8266_net_state_snapshot_t snapshot;
#if EV_ESP8266_NET_ENABLE_MQTT
    esp_mqtt_client_config_t mqtt_cfg;
#endif
    esp_err_t err;

    if (net == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    snapshot = ev_esp8266_net_snapshot_state(net);
    if (snapshot.initialized) {
        return EV_OK;
    }
    if (ev_esp8266_net_config_is_valid(&net->cfg) != EV_OK) {
        return EV_ERR_INVALID_ARG;
    }

    tcpip_adapter_init();
    err = esp_event_loop_init(ev_esp8266_wifi_event_handler, net);
    if (err != ESP_OK) {
        ev_esp8266_net_note_event_loop_failure(net);
        return EV_ERR_STATE;
    }
    ev_esp8266_net_mark_event_loop_owned(net);
    if (esp_wifi_init(&wifi_init_cfg) != ESP_OK) {
        return EV_ERR_STATE;
    }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK) {
        return EV_ERR_STATE;
    }
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
        return EV_ERR_STATE;
    }

    memset(&wifi_cfg, 0, sizeof(wifi_cfg));
    (void)strncpy((char *)wifi_cfg.sta.ssid, net->cfg.wifi_ssid, sizeof(wifi_cfg.sta.ssid) - 1U);
    (void)strncpy((char *)wifi_cfg.sta.password, net->cfg.wifi_password, sizeof(wifi_cfg.sta.password) - 1U);
    wifi_cfg.sta.threshold.authmode = ev_esp8266_net_auth_mode(net->cfg.wifi_auth_mode);
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK) {
        return EV_ERR_STATE;
    }

#if EV_ESP8266_NET_ENABLE_MQTT
    ev_esp8266_net_set_mqtt_supported(net, !ev_esp8266_net_string_is_empty(net->cfg.mqtt_broker_uri));
    snapshot = ev_esp8266_net_snapshot_state(net);
    if (snapshot.mqtt_supported) {
        memset(&mqtt_cfg, 0, sizeof(mqtt_cfg));
        mqtt_cfg.uri = net->cfg.mqtt_broker_uri;
        mqtt_cfg.client_id = net->cfg.mqtt_client_id;
        mqtt_cfg.event_handle = ev_esp8266_mqtt_event_handler;
        mqtt_cfg.user_context = net;
        snapshot.mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (snapshot.mqtt_client == NULL) {
            ev_esp8266_net_set_mqtt_supported(net, false);
            return EV_ERR_STATE;
        }
        ev_esp8266_net_lock();
        net->mqtt_client = snapshot.mqtt_client;
        ev_esp8266_net_unlock();
    } else {
        ev_esp8266_net_increment_counter(&net->mqtt_disabled);
    }
#else
    ev_esp8266_net_set_mqtt_supported(net, false);
    ev_esp8266_net_increment_counter(&net->mqtt_disabled);
#endif

    ev_esp8266_net_set_initialized(net, true);
    return EV_OK;
}

static ev_result_t ev_esp8266_net_start_fn(void *ctx)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;
    ev_esp8266_net_state_snapshot_t snapshot;

    if (net == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    snapshot = ev_esp8266_net_snapshot_state(net);
    if (!snapshot.initialized) {
        return EV_ERR_STATE;
    }
    if (!snapshot.wifi_started) {
        if (esp_wifi_start() != ESP_OK) {
            return EV_ERR_STATE;
        }
        ev_esp8266_net_set_wifi_started(net, true);
    }
    if (esp_wifi_connect() != ESP_OK) {
        return EV_ERR_STATE;
    }
    return EV_OK;
}

static ev_result_t ev_esp8266_net_stop_fn(void *ctx)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;
    ev_esp8266_net_state_snapshot_t snapshot;

    if (net == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    snapshot = ev_esp8266_net_snapshot_state(net);
#if EV_ESP8266_NET_ENABLE_MQTT
    if ((snapshot.mqtt_client != NULL) && snapshot.mqtt_started) {
        (void)esp_mqtt_client_stop(snapshot.mqtt_client);
        ev_esp8266_net_set_mqtt_started(net, false);
        (void)ev_esp8266_net_note_mqtt_down(net);
    }
#endif
    snapshot = ev_esp8266_net_snapshot_state(net);
    if (snapshot.wifi_started) {
        (void)esp_wifi_disconnect();
        (void)esp_wifi_stop();
        ev_esp8266_net_set_wifi_started(net, false);
        ev_esp8266_net_lock();
        net->wifi_connected = false;
        ++net->callback_state_updates;
        ev_esp8266_net_unlock();
    }
    return EV_OK;
}

static ev_result_t ev_esp8266_net_publish_mqtt_fn(void *ctx, const ev_net_mqtt_publish_cmd_t *cmd)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;

    if ((net == NULL) || (cmd == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    ++net->tx_attempts;
    ev_esp8266_net_unlock();

#if EV_ESP8266_NET_ENABLE_MQTT
    {
        ev_esp8266_net_state_snapshot_t snapshot;
        char topic[EV_NET_MAX_TOPIC_BYTES + 1U];
        int msg_id;

        snapshot = ev_esp8266_net_snapshot_state(net);
        if (!snapshot.mqtt_supported || (snapshot.mqtt_client == NULL)) {
            ev_esp8266_net_lock();
            ++net->mqtt_disabled;
            ++net->tx_failed;
            ++net->tx_rejected_state;
            ev_esp8266_net_unlock();
            return EV_ERR_UNSUPPORTED;
        }
        if (!snapshot.mqtt_connected) {
            ev_esp8266_net_lock();
            ++net->tx_failed;
            ++net->tx_rejected_state;
            ev_esp8266_net_unlock();
            return EV_ERR_STATE;
        }
        if ((cmd->topic_len > EV_NET_MAX_TOPIC_BYTES) ||
            (cmd->payload_len > EV_NET_MAX_INLINE_PAYLOAD_BYTES) ||
            (cmd->topic_len == 0U)) {
            ev_esp8266_net_lock();
            ++net->tx_failed;
            ++net->tx_rejected_oversize;
            ev_esp8266_net_unlock();
            return EV_ERR_INVALID_ARG;
        }

        memset(topic, 0, sizeof(topic));
        memcpy(topic, cmd->topic, cmd->topic_len);
        msg_id = esp_mqtt_client_publish(snapshot.mqtt_client,
                                         topic,
                                         (const char *)cmd->payload,
                                         (int)cmd->payload_len,
                                         (int)cmd->qos,
                                         (int)cmd->retain);
        if (msg_id < 0) {
            ev_esp8266_net_increment_counter(&net->tx_failed);
            return EV_ERR_STATE;
        }

        ev_esp8266_net_increment_counter(&net->tx_ok);
        return EV_OK;
    }
#else
    (void)cmd;
    ev_esp8266_net_lock();
    ++net->mqtt_disabled;
    ++net->tx_failed;
    ++net->tx_rejected_state;
    ev_esp8266_net_unlock();
    return EV_ERR_UNSUPPORTED;
#endif
}

static ev_result_t ev_esp8266_net_publish_mqtt_view_fn(void *ctx, const ev_net_mqtt_publish_view_t *view)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;

    if ((net == NULL) || (view == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    ++net->tx_attempts;
    ev_esp8266_net_unlock();

#if EV_ESP8266_NET_ENABLE_MQTT
    {
        ev_esp8266_net_state_snapshot_t snapshot;
        char topic[EV_NET_MAX_TOPIC_STORAGE_BYTES + 1U];
        int msg_id;

        snapshot = ev_esp8266_net_snapshot_state(net);
        if (!snapshot.mqtt_supported || (snapshot.mqtt_client == NULL)) {
            ev_esp8266_net_lock();
            ++net->mqtt_disabled;
            ++net->tx_failed;
            ++net->tx_rejected_state;
            ev_esp8266_net_unlock();
            return EV_ERR_UNSUPPORTED;
        }
        if (!snapshot.mqtt_connected) {
            ev_esp8266_net_lock();
            ++net->tx_failed;
            ++net->tx_rejected_state;
            ev_esp8266_net_unlock();
            return EV_ERR_STATE;
        }
        if ((view->topic == NULL) || (view->topic_len == 0U) ||
            (view->topic_len > EV_NET_MAX_TOPIC_STORAGE_BYTES) ||
            ((view->payload_len > 0U) && (view->payload == NULL)) ||
            (view->payload_len > EV_NET_MAX_PAYLOAD_STORAGE_BYTES)) {
            ev_esp8266_net_lock();
            ++net->tx_failed;
            ++net->tx_rejected_oversize;
            ev_esp8266_net_unlock();
            return EV_ERR_INVALID_ARG;
        }

        memset(topic, 0, sizeof(topic));
        memcpy(topic, view->topic, view->topic_len);
        msg_id = esp_mqtt_client_publish(snapshot.mqtt_client,
                                         topic,
                                         (const char *)view->payload,
                                         (int)view->payload_len,
                                         (int)view->qos,
                                         (int)view->retain);
        if (msg_id < 0) {
            ev_esp8266_net_increment_counter(&net->tx_failed);
            return EV_ERR_STATE;
        }

        ev_esp8266_net_increment_counter(&net->tx_ok);
        return EV_OK;
    }
#else
    (void)view;
    ev_esp8266_net_lock();
    ++net->mqtt_disabled;
    ++net->tx_failed;
    ++net->tx_rejected_state;
    ev_esp8266_net_unlock();
    return EV_ERR_UNSUPPORTED;
#endif
}

static ev_result_t ev_esp8266_net_poll_ingress_fn(void *ctx, ev_net_ingress_event_t *out_event)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;
    uint32_t index;

    if ((net == NULL) || (out_event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    if (ev_esp8266_net_pending_unsafe(net) == 0U) {
        ev_esp8266_net_unlock();
        return EV_ERR_EMPTY;
    }

    index = net->read_seq & EV_NET_INGRESS_RING_MASK;
    *out_event = net->ring[index];
    ++net->read_seq;
    ev_esp8266_net_unlock();
    return EV_OK;
}

static ev_result_t ev_esp8266_net_get_stats_fn(void *ctx, ev_net_stats_t *out_stats)
{
    ev_esp8266_net_ctx_t *net = (ev_esp8266_net_ctx_t *)ctx;

    if ((net == NULL) || (out_stats == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_esp8266_net_lock();
    memset(out_stats, 0, sizeof(*out_stats));
    out_stats->write_seq = net->write_seq;
    out_stats->read_seq = net->read_seq;
    out_stats->pending_events = ev_esp8266_net_pending_unsafe(net);
    out_stats->dropped_events = net->dropped_events;
    out_stats->dropped_oversize = net->dropped_oversize;
    out_stats->dropped_no_payload_slot = net->dropped_no_payload_slot;
    out_stats->payload_slots_total = EV_NET_PAYLOAD_SLOT_COUNT;
    out_stats->payload_slots_in_use = net->payload_slots_in_use;
    out_stats->payload_slots_high_watermark = net->payload_slots_high_watermark;
    out_stats->payload_slot_acquire_ok = net->payload_slot_acquire_ok;
    out_stats->payload_slot_acquire_failed = net->payload_slot_acquire_failed;
    out_stats->payload_slot_release_count = net->payload_slot_release_count;
    out_stats->high_watermark = net->high_watermark;
    out_stats->wifi_up_events = net->wifi_up_events;
    out_stats->wifi_down_events = net->wifi_down_events;
    out_stats->reconnect_attempts = net->reconnect_attempts;
    out_stats->reconnect_suppressed = net->reconnect_suppressed;
    out_stats->duplicate_wifi_down_suppressed = net->duplicate_wifi_down_suppressed;
    out_stats->duplicate_wifi_up_suppressed = net->duplicate_wifi_up_suppressed;
    out_stats->event_loop_init_failures = net->event_loop_init_failures;
    out_stats->event_loop_already_initialized = net->event_loop_already_initialized;
    out_stats->state_snapshots = net->state_snapshots;
    out_stats->callback_state_updates = net->callback_state_updates;
    out_stats->mqtt_disabled = net->mqtt_disabled;
    out_stats->mqtt_connect_attempts = net->mqtt_connect_attempts;
    out_stats->mqtt_up_events = net->mqtt_up_events;
    out_stats->mqtt_down_events = net->mqtt_down_events;
    out_stats->mqtt_rx_events = net->mqtt_rx_events;
    out_stats->mqtt_rx_bytes = net->mqtt_rx_bytes;
    out_stats->mqtt_rx_inline_events = net->mqtt_rx_inline_events;
    out_stats->mqtt_rx_slot_events = net->mqtt_rx_slot_events;
    out_stats->tx_attempts = net->tx_attempts;
    out_stats->tx_ok = net->tx_ok;
    out_stats->tx_failed = net->tx_failed;
    out_stats->tx_rejected_state = net->tx_rejected_state;
    out_stats->tx_rejected_oversize = net->tx_rejected_oversize;
    ev_esp8266_net_unlock();
    return EV_OK;
}

ev_result_t ev_esp8266_net_port_init(ev_net_port_t *out_port, const ev_esp8266_net_config_t *cfg)
{
    if ((out_port == NULL) || (cfg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_esp8266_net_config_is_valid(cfg) != EV_OK) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&g_ev_esp8266_net_ctx, 0, sizeof(g_ev_esp8266_net_ctx));
    g_ev_esp8266_net_ctx.cfg = *cfg;
    memset(out_port, 0, sizeof(*out_port));
    out_port->ctx = &g_ev_esp8266_net_ctx;
    out_port->init = ev_esp8266_net_init_fn;
    out_port->start = ev_esp8266_net_start_fn;
    out_port->stop = ev_esp8266_net_stop_fn;
    out_port->publish_mqtt = ev_esp8266_net_publish_mqtt_fn;
    out_port->publish_mqtt_view = ev_esp8266_net_publish_mqtt_view_fn;
    out_port->poll_ingress = ev_esp8266_net_poll_ingress_fn;
    out_port->get_stats = ev_esp8266_net_get_stats_fn;
    return EV_OK;
}
