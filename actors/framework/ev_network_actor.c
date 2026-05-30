#include "ev/network_actor.h"

#include <stdio.h>
#include <string.h>

#include "ev/ds18b20_actor.h"
#include "ev/mcp23008_actor.h"
#include "ev/rtc_actor.h"

static bool ev_network_message_has_exact_payload(const ev_msg_t *msg, size_t payload_size)
{
    return (msg != NULL) && (ev_msg_payload_size(msg) == payload_size) &&
           ((payload_size == 0U) || (ev_msg_payload_data(msg) != NULL));
}

static bool ev_network_telemetry_rate_limited(const ev_network_actor_ctx_t *ctx, uint32_t last_tick)
{
    if (ctx == NULL) {
        return true;
    }
    if (last_tick == EV_NETWORK_TELEMETRY_TICK_NEVER) {
        return false;
    }
    return (uint32_t)(ctx->telemetry_tick_counter - last_tick) < EV_NETWORK_TELEMETRY_MIN_INTERVAL_TICKS;
}

static bool ev_network_copy_topic(char *dst, size_t dst_len, const char *topic, size_t topic_len)
{
    if ((dst == NULL) || (topic == NULL) || (dst_len == 0U) || (topic_len == 0U) || (topic_len >= dst_len)) {
        return false;
    }
    memcpy(dst, topic, topic_len);
    dst[topic_len] = '\0';
    return true;
}

static bool ev_network_format_checked(char *dst, size_t dst_len, const char *fmt, uint32_t a, uint32_t b)
{
    int written;

    if ((dst == NULL) || (dst_len == 0U) || (fmt == NULL)) {
        return false;
    }

    written = snprintf(dst, dst_len, fmt, (unsigned long)a, (unsigned long)b);
    return (written >= 0) && ((size_t)written < dst_len);
}

static ev_result_t ev_network_publish_telemetry(ev_network_actor_ctx_t *ctx,
                                                const char *topic,
                                                char *payload,
                                                size_t payload_len,
                                                uint32_t *last_sent_tick)
{
    ++ctx->stats.telemetry_seen;
#if EV_NETWORK_TELEMETRY_ENABLED == 0
    (void)topic;
    (void)payload;
    (void)payload_len;
    (void)last_sent_tick;
    ++ctx->stats.telemetry_dropped_disabled;
    return EV_OK;
#else
    ev_net_mqtt_publish_view_t view;
    const size_t topic_len = (topic != NULL) ? strlen(topic) : 0U;
    ev_result_t rc;
    if ((ctx->state != EV_NETWORK_STATE_MQTT_CONNECTED) || (ctx->net_port == NULL)) {
        ++ctx->stats.telemetry_dropped_disconnected;
        return EV_OK;
    }
    if (ev_network_telemetry_rate_limited(ctx, (last_sent_tick != NULL) ? *last_sent_tick : 0U)) {
        ++ctx->stats.telemetry_dropped_rate_limit;
        return EV_OK;
    }
    if ((topic == NULL) || (payload == NULL) || (payload_len == 0U) ||
        (topic_len == 0U) || (topic_len >= EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES) ||
        (payload_len >= EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES)) {
        ++ctx->stats.telemetry_dropped_oversize;
        return EV_OK;
    }
    if (ctx->net_port->publish_mqtt_view == NULL) {
        ++ctx->stats.telemetry_dropped_unsupported;
        return EV_OK;
    }

    memset(&view, 0, sizeof(view));
    view.topic = topic;
    view.topic_len = topic_len;
    view.payload = (const uint8_t *)payload;
    view.payload_len = payload_len;
    view.qos = 0U;
    view.retain = 0U;

    rc = ctx->net_port->publish_mqtt_view(ctx->net_port->ctx, &view);
    if (rc == EV_OK) {
        ++ctx->stats.telemetry_sent;
        if (last_sent_tick != NULL) {
            *last_sent_tick = ctx->telemetry_tick_counter;
        }
        return EV_OK;
    }
    if (rc == EV_ERR_UNSUPPORTED) {
        ++ctx->stats.telemetry_dropped_unsupported;
        return EV_OK;
    }
    if (rc == EV_ERR_STATE) {
        ++ctx->stats.telemetry_dropped_disconnected;
        return EV_OK;
    }

    ++ctx->stats.tx_failed;
    ++ctx->stats.telemetry_dropped_unsupported;
    return EV_OK;
#endif
}

static ev_result_t ev_network_actor_handle_temp_updated(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_temp_payload_t *payload;
    char topic[EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES];
    char body[EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES];

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_temp_payload_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }
    payload = (const ev_temp_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.telemetry_temp_seen;
    if (!ev_network_copy_topic(topic, sizeof(topic), "telemetry/temp", sizeof("telemetry/temp") - 1U)) {
        ++ctx->stats.telemetry_seen;
        ++ctx->stats.telemetry_dropped_oversize;
        return EV_OK;
    }
    {
        const int written = snprintf(body, sizeof(body), "{\"cC\":%d}", (int)payload->centi_celsius);
        if ((written < 0) || ((size_t)written >= sizeof(body))) {
            ++ctx->stats.telemetry_seen;
            ++ctx->stats.telemetry_dropped_oversize;
            return EV_OK;
        }
    }
    return ev_network_publish_telemetry(ctx, topic, body, strlen(body), &ctx->last_temp_telemetry_tick);
}

static ev_result_t ev_network_actor_handle_time_updated(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_time_payload_t *payload;
    char topic[EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES];
    char body[EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES];

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_time_payload_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }
    payload = (const ev_time_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.telemetry_time_seen;
    if (!ev_network_copy_topic(topic, sizeof(topic), "telemetry/time", sizeof("telemetry/time") - 1U) ||
        !ev_network_format_checked(body, sizeof(body), "{\"unix\":%lu}", payload->unix_time, 0U)) {
        ++ctx->stats.telemetry_seen;
        ++ctx->stats.telemetry_dropped_oversize;
        return EV_OK;
    }
    return ev_network_publish_telemetry(ctx, topic, body, strlen(body), &ctx->last_time_telemetry_tick);
}

static ev_result_t ev_network_actor_handle_inputs_changed(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_mcp23008_input_payload_t *payload;
    char topic[EV_NETWORK_TELEMETRY_TOPIC_MAX_BYTES];
    char body[EV_NETWORK_TELEMETRY_PAYLOAD_MAX_BYTES];

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_mcp23008_input_payload_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }
    payload = (const ev_mcp23008_input_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.telemetry_inputs_seen;
    if (!ev_network_copy_topic(topic, sizeof(topic), "telemetry/inputs", sizeof("telemetry/inputs") - 1U) ||
        !ev_network_format_checked(body,
                                   sizeof(body),
                                   "{\"pressed\":%lu,\"changed\":%lu}",
                                   payload->pressed_mask,
                                   payload->changed_mask)) {
        ++ctx->stats.telemetry_seen;
        ++ctx->stats.telemetry_dropped_oversize;
        return EV_OK;
    }
    return ev_network_publish_telemetry(ctx, topic, body, strlen(body), &ctx->last_inputs_telemetry_tick);
}

ev_result_t ev_network_actor_init(ev_network_actor_ctx_t *ctx, ev_net_port_t *net_port)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->net_port = net_port;
    ctx->state = EV_NETWORK_STATE_DISCONNECTED;
    ctx->last_temp_telemetry_tick = EV_NETWORK_TELEMETRY_TICK_NEVER;
    ctx->last_time_telemetry_tick = EV_NETWORK_TELEMETRY_TICK_NEVER;
    ctx->last_inputs_telemetry_tick = EV_NETWORK_TELEMETRY_TICK_NEVER;
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_wifi_up(ev_network_actor_ctx_t *ctx)
{
    ++ctx->stats.wifi_up_events;
    if (ctx->state == EV_NETWORK_STATE_DISCONNECTED) {
        ctx->state = EV_NETWORK_STATE_WIFI_UP;
    }
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_wifi_down(ev_network_actor_ctx_t *ctx)
{
    ++ctx->stats.wifi_down_events;
    ctx->state = EV_NETWORK_STATE_DISCONNECTED;
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_mqtt_up(ev_network_actor_ctx_t *ctx)
{
    ++ctx->stats.mqtt_up_events;
    ctx->state = EV_NETWORK_STATE_MQTT_CONNECTED;
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_mqtt_down(ev_network_actor_ctx_t *ctx)
{
    ++ctx->stats.mqtt_down_events;
    ctx->state = EV_NETWORK_STATE_WIFI_UP;
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_mqtt_rx_inline(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_net_mqtt_inline_payload_t *rx;

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_net_mqtt_inline_payload_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }

    rx = (const ev_net_mqtt_inline_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.mqtt_rx_events;
    ++ctx->stats.mqtt_rx_inline_events;
    ++ctx->stats.mqtt_rx_ignored_foundation;
    if (rx != NULL) {
        ctx->stats.mqtt_rx_bytes += rx->payload_len;
    }
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_mqtt_rx_lease(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_net_mqtt_rx_payload_t *rx;

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_net_mqtt_rx_payload_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }

    rx = (const ev_net_mqtt_rx_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.mqtt_rx_events;
    ++ctx->stats.mqtt_rx_slot_events;
    ++ctx->stats.mqtt_rx_ignored_foundation;
    if (rx != NULL) {
        ctx->stats.mqtt_rx_bytes += rx->payload_len;
    }
    return EV_OK;
}

static ev_result_t ev_network_actor_handle_tx(ev_network_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_net_mqtt_publish_cmd_t *cmd;
    ev_result_t rc;

    if (!ev_network_message_has_exact_payload(msg, sizeof(ev_net_mqtt_publish_cmd_t))) {
        ++ctx->stats.bad_payloads;
        return EV_ERR_CONTRACT;
    }

    ++ctx->stats.tx_commands_seen;
    if (ctx->state != EV_NETWORK_STATE_MQTT_CONNECTED) {
        ++ctx->stats.tx_rejected_not_connected;
        return EV_OK;
    }
    if ((ctx->net_port == NULL) || (ctx->net_port->publish_mqtt == NULL)) {
        ++ctx->stats.tx_failed;
        return EV_ERR_UNSUPPORTED;
    }

    cmd = (const ev_net_mqtt_publish_cmd_t *)ev_msg_payload_data(msg);
    ++ctx->stats.tx_attempts;
    rc = ctx->net_port->publish_mqtt(ctx->net_port->ctx, cmd);
    if (rc == EV_OK) {
        ++ctx->stats.tx_ok;
    } else {
        ++ctx->stats.tx_failed;
    }
    return rc;
}

ev_result_t ev_network_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_network_actor_ctx_t *ctx = (ev_network_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_TICK_1S:
        ++ctx->stats.telemetry_ticks;
        ++ctx->telemetry_tick_counter;
        return EV_OK;
    case EV_NET_WIFI_UP:
        return ev_network_actor_handle_wifi_up(ctx);
    case EV_NET_WIFI_DOWN:
        return ev_network_actor_handle_wifi_down(ctx);
    case EV_NET_MQTT_UP:
        return ev_network_actor_handle_mqtt_up(ctx);
    case EV_NET_MQTT_DOWN:
        return ev_network_actor_handle_mqtt_down(ctx);
    case EV_NET_MQTT_MSG_RX:
        return ev_network_actor_handle_mqtt_rx_inline(ctx, msg);
    case EV_NET_MQTT_MSG_RX_LEASE:
        return ev_network_actor_handle_mqtt_rx_lease(ctx, msg);
    case EV_NET_TX_CMD:
        return ev_network_actor_handle_tx(ctx, msg);
    case EV_TEMP_UPDATED:
        return ev_network_actor_handle_temp_updated(ctx, msg);
    case EV_TIME_UPDATED:
        return ev_network_actor_handle_time_updated(ctx, msg);
    case EV_MCP23008_INPUT_CHANGED:
        return ev_network_actor_handle_inputs_changed(ctx, msg);
    default:
        ++ctx->stats.bad_events;
        return EV_ERR_CONTRACT;
    }
}

const ev_network_actor_stats_t *ev_network_actor_stats(const ev_network_actor_ctx_t *ctx)
{
    return (ctx != NULL) ? &ctx->stats : NULL;
}
