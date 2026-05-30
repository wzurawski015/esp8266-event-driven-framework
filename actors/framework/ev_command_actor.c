#include "ev/command_actor.h"

#include "ev/dispose.h"
#include <string.h>

#include "ev/mcp23008_actor.h"
#include "ev/oled_actor.h"
#include "ev/port_net.h"
#include "ev/power_actor.h"
#include "ev/publish.h"

#define EV_COMMAND_TOPIC_LED "cmd/led"
#define EV_COMMAND_TOPIC_DISPLAY "cmd/display"
#define EV_COMMAND_TOPIC_SLEEP "cmd/sleep"

#define EV_COMMAND_KEY_TOKEN "token"
#define EV_COMMAND_KEY_MASK "mask"
#define EV_COMMAND_KEY_VALID "valid"
#define EV_COMMAND_KEY_TEXT "text"
#define EV_COMMAND_KEY_ARM "arm"
#define EV_COMMAND_KEY_CONFIRM "confirm"
#define EV_COMMAND_KEY_MS "ms"

#define EV_COMMAND_U32_MAX_DECIMAL_DIGITS 10U

typedef struct ev_command_kv_view {
    const char *key;
    size_t key_len;
    const char *value;
    size_t value_len;
} ev_command_kv_view_t;

typedef struct ev_command_parse_state {
    const char *token;
    size_t token_len;
    const char *mask;
    size_t mask_len;
    const char *valid;
    size_t valid_len;
    const char *text;
    size_t text_len;
    const char *arm;
    size_t arm_len;
    const char *confirm;
    size_t confirm_len;
    const char *ms;
    size_t ms_len;
} ev_command_parse_state_t;

static size_t ev_command_bounded_cstr_len(const char *s, size_t max_len)
{
    size_t len = 0U;
    if (s == NULL) {
        return 0U;
    }
    while ((len < max_len) && (s[len] != '\0')) {
        ++len;
    }
    return len;
}

static bool ev_command_bytes_equal(const char *a, size_t a_len, const char *b, size_t b_len)
{
    return (a != NULL) && (b != NULL) && (a_len == b_len) && (memcmp(a, b, a_len) == 0);
}

static bool ev_command_topic_is(const char *topic, size_t topic_len, const char *literal)
{
    return ev_command_bytes_equal(topic, topic_len, literal, strlen(literal));
}

static bool ev_command_ascii_printable(const char *data, size_t len)
{
    size_t i;
    if ((data == NULL) && (len > 0U)) {
        return false;
    }
    for (i = 0U; i < len; ++i) {
        const unsigned char ch = (unsigned char)data[i];
        if ((ch < 0x20U) || (ch > 0x7EU)) {
            return false;
        }
    }
    return true;
}

static bool ev_command_parse_uint32_dec(const char *data, size_t len, uint32_t *out)
{
    size_t i;
    uint32_t value = 0U;
    if ((data == NULL) || (out == NULL) || (len == 0U) || (len > EV_COMMAND_U32_MAX_DECIMAL_DIGITS)) {
        return false;
    }
    for (i = 0U; i < len; ++i) {
        const unsigned char ch = (unsigned char)data[i];
        uint32_t digit;
        if ((ch < (unsigned char)'0') || (ch > (unsigned char)'9')) {
            return false;
        }
        digit = (uint32_t)(ch - (unsigned char)'0');
        if (value > ((UINT32_MAX - digit) / 10U)) {
            return false;
        }
        value = (uint32_t)((value * 10U) + digit);
    }
    *out = value;
    return true;
}

static bool ev_command_hex_digit(unsigned char ch, uint8_t *out)
{
    if (out == NULL) {
        return false;
    }
    if ((ch >= (unsigned char)'0') && (ch <= (unsigned char)'9')) {
        *out = (uint8_t)(ch - (unsigned char)'0');
        return true;
    }
    if ((ch >= (unsigned char)'a') && (ch <= (unsigned char)'f')) {
        *out = (uint8_t)(10U + (uint8_t)(ch - (unsigned char)'a'));
        return true;
    }
    if ((ch >= (unsigned char)'A') && (ch <= (unsigned char)'F')) {
        *out = (uint8_t)(10U + (uint8_t)(ch - (unsigned char)'A'));
        return true;
    }
    return false;
}

static bool ev_command_parse_u8_hex(const char *data, size_t len, uint8_t *out)
{
    size_t i;
    uint8_t value = 0U;
    if ((data == NULL) || (out == NULL) || (len == 0U) || (len > 2U)) {
        return false;
    }
    for (i = 0U; i < len; ++i) {
        uint8_t nibble;
        if (!ev_command_hex_digit((unsigned char)data[i], &nibble)) {
            return false;
        }
        value = (uint8_t)((value << 4U) | nibble);
    }
    *out = value;
    return true;
}

static bool ev_command_parse_next_kv(const char *payload, size_t payload_len, size_t *cursor, ev_command_kv_view_t *out)
{
    size_t start;
    size_t eq;
    size_t end;
    if ((payload == NULL) || (cursor == NULL) || (out == NULL) || (*cursor > payload_len)) {
        return false;
    }
    if (*cursor == payload_len) {
        return false;
    }
    start = *cursor;
    eq = start;
    while ((eq < payload_len) && (payload[eq] != '=') && (payload[eq] != ';')) {
        ++eq;
    }
    if ((eq == start) || (eq >= payload_len) || (payload[eq] != '=')) {
        return false;
    }
    end = eq + 1U;
    while ((end < payload_len) && (payload[end] != ';')) {
        ++end;
    }
    out->key = &payload[start];
    out->key_len = eq - start;
    out->value = &payload[eq + 1U];
    out->value_len = end - (eq + 1U);
    *cursor = (end < payload_len) ? (end + 1U) : end;
    return true;
}

static bool ev_command_record_kv(ev_command_parse_state_t *state, const ev_command_kv_view_t *kv)
{
    if ((state == NULL) || (kv == NULL) || (kv->key == NULL) || (kv->value == NULL)) {
        return false;
    }
#define EV_COMMAND_MATCH_KEY(lit) ev_command_bytes_equal(kv->key, kv->key_len, (lit), sizeof(lit) - 1U)
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_TOKEN)) {
        state->token = kv->value;
        state->token_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_MASK)) {
        state->mask = kv->value;
        state->mask_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_VALID)) {
        state->valid = kv->value;
        state->valid_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_TEXT)) {
        state->text = kv->value;
        state->text_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_ARM)) {
        state->arm = kv->value;
        state->arm_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_CONFIRM)) {
        state->confirm = kv->value;
        state->confirm_len = kv->value_len;
        return true;
    }
    if (EV_COMMAND_MATCH_KEY(EV_COMMAND_KEY_MS)) {
        state->ms = kv->value;
        state->ms_len = kv->value_len;
        return true;
    }
#undef EV_COMMAND_MATCH_KEY
    return true;
}

static bool ev_command_parse_payload(const char *payload, size_t payload_len, ev_command_parse_state_t *state)
{
    size_t cursor = 0U;
    if ((payload == NULL) || (state == NULL) || (payload_len == 0U) || (payload_len > EV_COMMAND_PAYLOAD_MAX_BYTES) ||
        !ev_command_ascii_printable(payload, payload_len)) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    while (cursor < payload_len) {
        ev_command_kv_view_t kv;
        if (!ev_command_parse_next_kv(payload, payload_len, &cursor, &kv) || !ev_command_record_kv(state, &kv)) {
            return false;
        }
    }
    return true;
}

static bool ev_command_authorized(const ev_command_actor_ctx_t *ctx, const ev_command_parse_state_t *state)
{
    return (ctx != NULL) && (state != NULL) && (ctx->token != NULL) && (ctx->token_len > 0U) &&
           (state->token != NULL) && ev_command_bytes_equal(state->token, state->token_len, ctx->token, ctx->token_len);
}

static bool ev_command_rate_limited(const ev_command_actor_ctx_t *ctx, uint32_t last_tick)
{
    if (ctx == NULL) {
        return true;
    }
    return (uint32_t)(ctx->tick_counter - last_tick) < EV_COMMAND_MIN_INTERVAL_TICKS;
}

static ev_result_t ev_command_publish_inline(ev_command_actor_ctx_t *ctx,
                                             ev_event_id_t event_id,
                                             const void *payload,
                                             size_t payload_size)
{
    ev_msg_t out = {0};
    ev_result_t rc;
    if ((ctx == NULL) || (ctx->deliver == NULL) || (payload == NULL) || (payload_size == 0U)) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_msg_init_publish(&out, event_id, ACT_COMMAND);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&out, payload, payload_size);
    }
    if (rc == EV_OK) {
        rc = ev_publish(&out, ctx->deliver, ctx->deliver_context, NULL);
    }
    if (rc != EV_OK) {
        ++ctx->stats.publish_failures;
    }
    (void)ev_msg_dispose(&out);
    return rc;
}

static ev_result_t ev_command_handle_led(ev_command_actor_ctx_t *ctx, const ev_command_parse_state_t *state)
{
    ev_panel_led_set_cmd_t cmd;
    uint8_t mask;
    uint8_t valid;
    if ((ctx->capabilities & EV_COMMAND_CAP_LED) == 0U) {
        ++ctx->stats.capability_rejects;
        return EV_OK;
    }
    if (ev_command_rate_limited(ctx, ctx->last_led_tick)) {
        ++ctx->stats.rate_limited;
        return EV_OK;
    }
    if ((state->mask == NULL) || (state->valid == NULL) ||
        !ev_command_parse_u8_hex(state->mask, state->mask_len, &mask) ||
        !ev_command_parse_u8_hex(state->valid, state->valid_len, &valid)) {
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    cmd.value_mask = (uint8_t)(mask & EV_MCP23008_LED_MASK);
    cmd.valid_mask = (uint8_t)(valid & EV_MCP23008_LED_MASK);
    if (ev_command_publish_inline(ctx, EV_PANEL_LED_SET_CMD, &cmd, sizeof(cmd)) == EV_OK) {
        ++ctx->stats.commands_executed;
        ++ctx->stats.led_commands;
        ctx->last_led_tick = ctx->tick_counter;
    }
    return EV_OK;
}

static ev_result_t ev_command_handle_display(ev_command_actor_ctx_t *ctx, const ev_command_parse_state_t *state)
{
    ev_oled_display_text_cmd_t cmd;
    if ((ctx->capabilities & EV_COMMAND_CAP_DISPLAY) == 0U) {
        ++ctx->stats.capability_rejects;
        return EV_OK;
    }
    if (ev_command_rate_limited(ctx, ctx->last_display_tick)) {
        ++ctx->stats.rate_limited;
        return EV_OK;
    }
    if ((state->text == NULL) || (state->text_len == 0U) || (state->text_len >= EV_OLED_TEXT_MAX_CHARS) ||
        !ev_command_ascii_printable(state->text, state->text_len)) {
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    memset(&cmd, 0, sizeof(cmd));
    cmd.page = (uint8_t)EV_COMMAND_DISPLAY_PAGE;
    cmd.column = (uint8_t)EV_COMMAND_DISPLAY_COLUMN;
    memcpy(cmd.text, state->text, state->text_len);
    cmd.text[state->text_len] = '\0';
    if (ev_command_publish_inline(ctx, EV_OLED_DISPLAY_TEXT_CMD, &cmd, sizeof(cmd)) == EV_OK) {
        ++ctx->stats.commands_executed;
        ++ctx->stats.display_commands;
        ctx->last_display_tick = ctx->tick_counter;
    }
    return EV_OK;
}

static ev_result_t ev_command_handle_sleep_arm(ev_command_actor_ctx_t *ctx, const ev_command_parse_state_t *state)
{
    uint32_t duration_ms;
    if ((ctx->capabilities & EV_COMMAND_CAP_SLEEP) == 0U) {
        ++ctx->stats.capability_rejects;
        return EV_OK;
    }
    if (ev_command_rate_limited(ctx, ctx->last_sleep_tick)) {
        ++ctx->stats.rate_limited;
        return EV_OK;
    }
    if ((state->arm == NULL) || (state->ms == NULL) || (state->arm_len == 0U) ||
        (state->arm_len >= EV_COMMAND_SLEEP_NONCE_MAX_BYTES) ||
        !ev_command_ascii_printable(state->arm, state->arm_len) ||
        !ev_command_parse_uint32_dec(state->ms, state->ms_len, &duration_ms) ||
        (duration_ms == 0U) || (duration_ms > EV_POWER_MAX_SLEEP_DURATION_MS)) {
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    memset(ctx->sleep_nonce, 0, sizeof(ctx->sleep_nonce));
    memcpy(ctx->sleep_nonce, state->arm, state->arm_len);
    ctx->sleep_nonce_len = (uint8_t)state->arm_len;
    ctx->sleep_duration_ms = duration_ms;
    ctx->sleep_expiry_tick = ctx->tick_counter + EV_COMMAND_SLEEP_CONFIRM_WINDOW_TICKS;
    ctx->sleep_armed = true;
    ++ctx->stats.sleep_arm_requests;
    return EV_OK;
}

static ev_result_t ev_command_handle_sleep_confirm(ev_command_actor_ctx_t *ctx, const ev_command_parse_state_t *state)
{
    ev_sys_goto_sleep_cmd_t cmd;
    ++ctx->stats.sleep_confirm_requests;
    if ((ctx->capabilities & EV_COMMAND_CAP_SLEEP) == 0U) {
        ++ctx->stats.capability_rejects;
        return EV_OK;
    }
    if (!ctx->sleep_armed || (state->confirm == NULL) || (state->confirm_len != ctx->sleep_nonce_len) ||
        (memcmp(state->confirm, ctx->sleep_nonce, state->confirm_len) != 0)) {
        ++ctx->stats.sleep_confirm_mismatch;
        return EV_OK;
    }
    if ((uint32_t)(ctx->tick_counter - ctx->sleep_expiry_tick) < UINT32_MAX / 2U) {
        ctx->sleep_armed = false;
        ++ctx->stats.sleep_expired;
        return EV_OK;
    }
    cmd.duration_ms = ctx->sleep_duration_ms;
    ctx->sleep_armed = false;
    if (ev_command_publish_inline(ctx, EV_SYS_GOTO_SLEEP_CMD, &cmd, sizeof(cmd)) == EV_OK) {
        ++ctx->stats.commands_executed;
        ++ctx->stats.sleep_commands;
        ctx->last_sleep_tick = ctx->tick_counter;
    }
    return EV_OK;
}

static ev_result_t ev_command_dispatch(ev_command_actor_ctx_t *ctx,
                                       const char *topic,
                                       size_t topic_len,
                                       const char *payload,
                                       size_t payload_len)
{
    ev_command_parse_state_t state;
    ++ctx->stats.rx_seen;
    if ((topic == NULL) || (payload == NULL) || (topic_len == 0U) || (topic_len > EV_COMMAND_TOPIC_MAX_BYTES) ||
        (payload_len == 0U) || (payload_len > EV_COMMAND_PAYLOAD_MAX_BYTES)) {
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    if (!ev_command_parse_payload(payload, payload_len, &state)) {
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    if (!ev_command_authorized(ctx, &state)) {
        if ((ctx->token == NULL) || (ctx->token_len == 0U)) {
            ++ctx->stats.disabled_rejects;
        } else {
            ++ctx->stats.auth_rejects;
        }
        return EV_OK;
    }
    if (ev_command_topic_is(topic, topic_len, EV_COMMAND_TOPIC_LED)) {
        return ev_command_handle_led(ctx, &state);
    }
    if (ev_command_topic_is(topic, topic_len, EV_COMMAND_TOPIC_DISPLAY)) {
        return ev_command_handle_display(ctx, &state);
    }
    if (ev_command_topic_is(topic, topic_len, EV_COMMAND_TOPIC_SLEEP)) {
        if (state.arm != NULL) {
            return ev_command_handle_sleep_arm(ctx, &state);
        }
        if (state.confirm != NULL) {
            return ev_command_handle_sleep_confirm(ctx, &state);
        }
        ++ctx->stats.parse_errors;
        return EV_OK;
    }
    ++ctx->stats.unknown_topic;
    return EV_OK;
}

static ev_result_t ev_command_handle_inline(ev_command_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_net_mqtt_inline_payload_t *rx;
    if ((ctx == NULL) || (msg == NULL) || (ev_msg_payload_size(msg) != sizeof(ev_net_mqtt_inline_payload_t)) ||
        (ev_msg_payload_data(msg) == NULL)) {
        if (ctx != NULL) {
            ++ctx->stats.parse_errors;
        }
        return EV_ERR_CONTRACT;
    }
    rx = (const ev_net_mqtt_inline_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.rx_inline_seen;
    return ev_command_dispatch(ctx, rx->topic, rx->topic_len, (const char *)rx->payload, rx->payload_len);
}

static ev_result_t ev_command_handle_lease(ev_command_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_net_mqtt_rx_payload_t *rx;
    if ((ctx == NULL) || (msg == NULL) || (ev_msg_payload_size(msg) != sizeof(ev_net_mqtt_rx_payload_t)) ||
        (ev_msg_payload_data(msg) == NULL)) {
        if (ctx != NULL) {
            ++ctx->stats.parse_errors;
        }
        return EV_ERR_CONTRACT;
    }
    rx = (const ev_net_mqtt_rx_payload_t *)ev_msg_payload_data(msg);
    ++ctx->stats.rx_lease_seen;
    return ev_command_dispatch(ctx, rx->topic, rx->topic_len, (const char *)rx->payload, rx->payload_len);
}

static void ev_command_expire_sleep_arm(ev_command_actor_ctx_t *ctx)
{
    if ((ctx != NULL) && ctx->sleep_armed && ((uint32_t)(ctx->tick_counter - ctx->sleep_expiry_tick) < UINT32_MAX / 2U)) {
        ctx->sleep_armed = false;
        ++ctx->stats.sleep_expired;
    }
}

ev_result_t ev_command_actor_init(ev_command_actor_ctx_t *ctx,
                                  ev_delivery_fn_t deliver,
                                  void *deliver_context,
                                  const char *token,
                                  uint32_t capabilities)
{
    size_t token_len;
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    ctx->capabilities = capabilities;
    ctx->last_led_tick = UINT32_MAX;
    ctx->last_display_tick = UINT32_MAX;
    ctx->last_sleep_tick = UINT32_MAX;

    token_len = ev_command_bounded_cstr_len(token, EV_COMMAND_TOKEN_MAX_BYTES + 1U);
    if (token_len <= EV_COMMAND_TOKEN_MAX_BYTES) {
        ctx->token = token;
        ctx->token_len = token_len;
    } else {
        ctx->token = NULL;
        ctx->token_len = 0U;
    }
    return EV_OK;
}

ev_result_t ev_command_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_command_actor_ctx_t *ctx = (ev_command_actor_ctx_t *)actor_context;
    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_TICK_1S:
        ++ctx->tick_counter;
        ++ctx->stats.ticks_seen;
        ev_command_expire_sleep_arm(ctx);
        return EV_OK;
    case EV_NET_MQTT_MSG_RX:
        return ev_command_handle_inline(ctx, msg);
    case EV_NET_MQTT_MSG_RX_LEASE:
        return ev_command_handle_lease(ctx, msg);
    default:
        ++ctx->stats.unknown_topic;
        return EV_OK;
    }
}

const ev_command_actor_stats_t *ev_command_actor_stats(const ev_command_actor_ctx_t *ctx)
{
    return (ctx != NULL) ? &ctx->stats : NULL;
}
