#ifndef TESTS_HOST_FAKES_FAKE_NET_PORT_H
#define TESTS_HOST_FAKES_FAKE_NET_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/port_net.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fake_net_payload_slot {
    void *owner;
    bool in_use;
    uint32_t refcount;
    uint32_t generation;
    ev_net_mqtt_rx_payload_t payload;
} fake_net_payload_slot_t;

typedef struct fake_net_port {
    ev_net_ingress_event_t ring[EV_NET_INGRESS_RING_CAPACITY];
    fake_net_payload_slot_t payload_slots[EV_NET_PAYLOAD_SLOT_COUNT];
    uint32_t write_seq;
    uint32_t read_seq;
    uint32_t dropped_events;
    uint32_t dropped_oversize;
    uint32_t dropped_no_payload_slot;
    uint32_t high_watermark;
    uint32_t payload_slots_in_use;
    uint32_t payload_slots_high_watermark;
    uint32_t payload_slot_acquire_ok;
    uint32_t payload_slot_acquire_failed;
    uint32_t payload_slot_release_count;
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
    bool wifi_connected;
    bool mqtt_connected;
    uint32_t mqtt_disabled;
    uint32_t mqtt_connect_attempts;
    uint32_t mqtt_up_events;
    uint32_t mqtt_down_events;
    uint32_t mqtt_rx_events;
    uint32_t mqtt_rx_bytes;
    uint32_t mqtt_rx_inline_events;
    uint32_t mqtt_rx_slot_events;
    uint32_t tx_rejected_state;
    uint32_t tx_rejected_oversize;
    uint32_t init_calls;
    uint32_t start_calls;
    uint32_t stop_calls;
    uint32_t poll_calls;
    uint32_t get_stats_calls;
    uint32_t callback_push_calls;
    uint32_t publish_mqtt_calls;
    uint32_t publish_mqtt_view_calls;
    uint32_t publish_mqtt_ok;
    uint32_t publish_mqtt_failed;
    char last_publish_topic[EV_NET_MAX_TOPIC_STORAGE_BYTES + 1U];
    uint8_t last_publish_payload[EV_NET_MAX_PAYLOAD_STORAGE_BYTES];
    size_t last_publish_topic_len;
    size_t last_publish_payload_len;
    uint8_t last_publish_qos;
    uint8_t last_publish_retain;
    ev_result_t next_publish_result;
} fake_net_port_t;

void fake_net_port_init(fake_net_port_t *fake);
void fake_net_port_bind(ev_net_port_t *out_port, fake_net_port_t *fake);
ev_result_t fake_net_port_callback_push(fake_net_port_t *fake, const ev_net_ingress_event_t *event);
ev_result_t fake_net_port_callback_wifi_up(fake_net_port_t *fake);
ev_result_t fake_net_port_callback_wifi_down(fake_net_port_t *fake);
ev_result_t fake_net_port_callback_push_mqtt(fake_net_port_t *fake,
                                             const char *topic,
                                             size_t topic_len,
                                             const uint8_t *payload,
                                             size_t payload_len);
size_t fake_net_port_pending(const fake_net_port_t *fake);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_NET_PORT_H */
