#ifndef EV_PORT_NET_H
#define EV_PORT_NET_H

#include <stddef.h>
#include <stdint.h>

#include "ev/msg.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_NET_INGRESS_RING_CAPACITY 16U
#define EV_NET_INGRESS_RING_MASK (EV_NET_INGRESS_RING_CAPACITY - 1U)
#define EV_NET_MAX_TOPIC_BYTES 8U
#define EV_NET_MAX_INLINE_PAYLOAD_BYTES 8U

#ifndef EV_NET_MAX_TOPIC_STORAGE_BYTES
#define EV_NET_MAX_TOPIC_STORAGE_BYTES 64U
#endif
#ifndef EV_NET_MAX_PAYLOAD_STORAGE_BYTES
#define EV_NET_MAX_PAYLOAD_STORAGE_BYTES 128U
#endif
#ifndef EV_NET_PAYLOAD_SLOT_COUNT
#define EV_NET_PAYLOAD_SLOT_COUNT 4U
#endif

typedef enum ev_net_event_kind {
    EV_NET_EVENT_NONE = 0,
    EV_NET_EVENT_WIFI_UP = 1,
    EV_NET_EVENT_WIFI_DOWN = 2,
    EV_NET_EVENT_MQTT_UP = 3,
    EV_NET_EVENT_MQTT_DOWN = 4,
    EV_NET_EVENT_MQTT_MSG_RX = 5
} ev_net_event_kind_t;

typedef enum ev_net_payload_storage {
    EV_NET_PAYLOAD_NONE = 0,
    EV_NET_PAYLOAD_INLINE = 1,
    EV_NET_PAYLOAD_LEASE = 2
} ev_net_payload_storage_t;

typedef struct ev_net_mqtt_inline_payload {
    uint8_t topic_len;
    uint8_t payload_len;
    uint8_t qos;
    uint8_t retain;
    char topic[EV_NET_MAX_TOPIC_BYTES];
    uint8_t payload[EV_NET_MAX_INLINE_PAYLOAD_BYTES];
} ev_net_mqtt_inline_payload_t;

typedef struct ev_net_mqtt_rx_payload {
    uint8_t topic_len;
    uint8_t payload_len;
    uint8_t qos;
    uint8_t retain;
    char topic[EV_NET_MAX_TOPIC_STORAGE_BYTES];
    uint8_t payload[EV_NET_MAX_PAYLOAD_STORAGE_BYTES];
} ev_net_mqtt_rx_payload_t;

typedef struct ev_net_payload_lease {
    const void *data;
    size_t size;
    ev_msg_retain_fn_t retain_fn;
    ev_msg_release_fn_t release_fn;
    void *lifecycle_ctx;
} ev_net_payload_lease_t;

typedef struct ev_net_ingress_event {
    ev_net_event_kind_t kind;
    uint8_t topic_len;
    uint8_t payload_len;
    uint8_t payload_storage;
    uint8_t reserved;
    char topic[EV_NET_MAX_TOPIC_BYTES];
    uint8_t payload[EV_NET_MAX_INLINE_PAYLOAD_BYTES];
    ev_net_payload_lease_t external_payload;
} ev_net_ingress_event_t;

typedef ev_net_mqtt_inline_payload_t ev_net_mqtt_publish_cmd_t;

typedef struct ev_net_mqtt_publish_view {
    const char *topic;
    size_t topic_len;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t qos;
    uint8_t retain;
} ev_net_mqtt_publish_view_t;

typedef struct ev_net_stats {
    uint32_t write_seq;
    uint32_t read_seq;
    uint32_t pending_events;
    uint32_t dropped_events;
    uint32_t dropped_oversize;
    uint32_t dropped_no_payload_slot;
    uint32_t high_watermark;
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
    uint32_t mqtt_rx_bytes;
    uint32_t mqtt_rx_inline_events;
    uint32_t mqtt_rx_slot_events;
    uint32_t payload_slots_total;
    uint32_t payload_slots_in_use;
    uint32_t payload_slots_high_watermark;
    uint32_t payload_slot_acquire_ok;
    uint32_t payload_slot_acquire_failed;
    uint32_t payload_slot_release_count;
    uint32_t tx_attempts;
    uint32_t tx_ok;
    uint32_t tx_failed;
    uint32_t tx_rejected_state;
    uint32_t tx_rejected_oversize;
} ev_net_stats_t;

typedef ev_result_t (*ev_net_init_fn_t)(void *ctx);
typedef ev_result_t (*ev_net_start_fn_t)(void *ctx);
typedef ev_result_t (*ev_net_stop_fn_t)(void *ctx);
typedef ev_result_t (*ev_net_publish_mqtt_fn_t)(void *ctx, const ev_net_mqtt_publish_cmd_t *cmd);
typedef ev_result_t (*ev_net_publish_mqtt_view_fn_t)(void *ctx, const ev_net_mqtt_publish_view_t *view);
typedef ev_result_t (*ev_net_poll_ingress_fn_t)(void *ctx, ev_net_ingress_event_t *out_event);
typedef ev_result_t (*ev_net_get_stats_fn_t)(void *ctx, ev_net_stats_t *out_stats);

typedef struct ev_net_port {
    void *ctx;
    ev_net_init_fn_t init;
    ev_net_start_fn_t start;
    ev_net_stop_fn_t stop;
    ev_net_publish_mqtt_fn_t publish_mqtt;
    ev_net_publish_mqtt_view_fn_t publish_mqtt_view;
    ev_net_poll_ingress_fn_t poll_ingress;
    ev_net_get_stats_fn_t get_stats;
} ev_net_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_NET_H */
