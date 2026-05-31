#ifndef EV_NETWORK_ACTOR_H
#define EV_NETWORK_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/msg.h"
#include "ev/port_net.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EV_NETWORK_TELEMETRY_ENABLED
#define EV_NETWORK_TELEMETRY_ENABLED 1
#endif
#ifndef EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES
#define EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES 32U
#endif
#ifndef EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES
#define EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES 64U
#endif
#ifndef EV_NETWORK_TELEMETRY_MIN_INTERVAL_TICKS
#define EV_NETWORK_TELEMETRY_MIN_INTERVAL_TICKS 1U
#endif
#define EV_NETWORK_TELEMETRY_TICK_NEVER UINT32_MAX

typedef enum ev_network_state {
    EV_NETWORK_STATE_DISCONNECTED = 0,
    EV_NETWORK_STATE_WIFI_UP = 1,
    EV_NETWORK_STATE_MQTT_CONNECTED = 2,
    EV_NETWORK_STATE_DEGRADED = 3
} ev_network_state_t;

typedef struct ev_network_actor_stats {
    uint32_t wifi_up_events;
    uint32_t wifi_down_events;
    uint32_t mqtt_up_events;
    uint32_t mqtt_down_events;
    uint32_t mqtt_rx_events;
    uint32_t mqtt_rx_ignored_foundation;
    uint32_t mqtt_rx_bytes;
    uint32_t mqtt_rx_inline_events;
    uint32_t mqtt_rx_slot_events;
    uint32_t tx_commands_seen;
    uint32_t tx_attempts;
    uint32_t tx_ok;
    uint32_t tx_failed;
    uint32_t tx_rejected_not_connected;
    uint32_t bad_payloads;
    uint32_t bad_events;
    uint32_t telemetry_ticks;
    uint32_t telemetry_seen;
    uint32_t telemetry_sent;
    uint32_t telemetry_dropped_disabled;
    uint32_t telemetry_dropped_disconnected;
    uint32_t telemetry_dropped_rate_limit;
    uint32_t telemetry_dropped_oversize;
    uint32_t telemetry_dropped_unsupported;
    uint32_t telemetry_temp_seen;
    uint32_t telemetry_time_seen;
    uint32_t telemetry_inputs_seen;
} ev_network_actor_stats_t;

typedef struct ev_network_actor_ctx {
    ev_net_port_t *net_port;
    ev_network_state_t state;
    ev_network_actor_stats_t stats;
    uint32_t telemetry_tick_counter;
    uint32_t last_temp_telemetry_tick;
    uint32_t last_time_telemetry_tick;
    uint32_t last_inputs_telemetry_tick;
} ev_network_actor_ctx_t;

EV_STATIC_ASSERT(sizeof(ev_net_mqtt_inline_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "inline network MQTT RX payload must fit message payload");
EV_STATIC_ASSERT(sizeof(ev_net_mqtt_publish_cmd_t) <= EV_MSG_INLINE_CAPACITY,
                 "network tx command must fit inline message payload");
EV_STATIC_ASSERT((EV_NET_INGRESS_RING_CAPACITY & (EV_NET_INGRESS_RING_CAPACITY - 1U)) == 0U,
                 "network ingress ring capacity must be a power of two");
EV_STATIC_ASSERT(EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES <= EV_NET_MAX_TOPIC_STORAGE_BYTES,
                 "telemetry topic buffer must fit network storage limit");
EV_STATIC_ASSERT(EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES <= EV_NET_MAX_PAYLOAD_STORAGE_BYTES,
                 "telemetry payload buffer must fit network storage limit");

ev_result_t ev_network_actor_init(ev_network_actor_ctx_t *ctx, ev_net_port_t *net_port);
ev_result_t ev_network_actor_handle(void *actor_context, const ev_msg_t *msg);
const ev_network_actor_stats_t *ev_network_actor_stats(const ev_network_actor_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EV_NETWORK_ACTOR_H */
