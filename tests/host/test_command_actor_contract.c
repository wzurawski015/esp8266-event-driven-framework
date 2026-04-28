#include <assert.h>
#include <string.h>

#include "ev/command_actor.h"
#include "ev/mcp23008_actor.h"
#include "ev/oled_actor.h"
#include "ev/power_actor.h"
#include "ev/port_net.h"
#include "ev/dispose.h"


#if !defined(__GNUC__)
#error "host test expects a C11 compiler"
#endif


typedef struct test_payload_lease_state {
    ev_net_mqtt_rx_payload_t payload;
    uint32_t retain_count;
    uint32_t release_count;
    bool in_use;
} test_payload_lease_state_t;

static ev_result_t test_payload_retain(void *ctx, const void *payload, size_t payload_size)
{
    test_payload_lease_state_t *state = (test_payload_lease_state_t *)ctx;
    assert(state != NULL);
    assert(payload == &state->payload);
    assert(payload_size == sizeof(state->payload));
    assert(state->in_use);
    ++state->retain_count;
    return EV_OK;
}

static void test_payload_release(void *ctx, const void *payload, size_t payload_size)
{
    test_payload_lease_state_t *state = (test_payload_lease_state_t *)ctx;
    assert(state != NULL);
    assert(payload == &state->payload);
    assert(payload_size == sizeof(state->payload));
    assert(state->in_use);
    ++state->release_count;
    state->in_use = false;
}

static ev_net_payload_lease_t test_payload_make_lease(test_payload_lease_state_t *state,
                                                      const char *topic,
                                                      const char *payload)
{
    ev_net_payload_lease_t lease;
    size_t topic_len = strlen(topic);
    size_t payload_len = strlen(payload);
    assert(topic_len < EV_NET_MAX_TOPIC_STORAGE_BYTES);
    assert(payload_len < EV_NET_MAX_PAYLOAD_STORAGE_BYTES);
    memset(state, 0, sizeof(*state));
    state->in_use = true;
    state->retain_count = 1U;
    state->payload.topic_len = (uint8_t)topic_len;
    state->payload.payload_len = (uint8_t)payload_len;
    memcpy(state->payload.topic, topic, topic_len);
    memcpy(state->payload.payload, payload, payload_len);
    memset(&lease, 0, sizeof(lease));
    lease.data = &state->payload;
    lease.size = sizeof(state->payload);
    lease.retain_fn = test_payload_retain;
    lease.release_fn = test_payload_release;
    lease.lifecycle_ctx = state;
    return lease;
}

typedef struct capture_delivery {
    uint32_t calls;
    ev_actor_id_t last_target;
    ev_event_id_t last_event;
    unsigned char last_payload[EV_MSG_INLINE_CAPACITY];
    size_t last_payload_size;
} capture_delivery_t;

static ev_result_t capture_deliver(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    capture_delivery_t *capture = (capture_delivery_t *)context;
    assert(capture != NULL);
    assert(msg != NULL);
    ++capture->calls;
    capture->last_target = target_actor;
    capture->last_event = msg->event_id;
    capture->last_payload_size = ev_msg_payload_size(msg);
    memset(capture->last_payload, 0, sizeof(capture->last_payload));
    if (capture->last_payload_size > 0U) {
        assert(capture->last_payload_size <= sizeof(capture->last_payload));
        memcpy(capture->last_payload, ev_msg_payload_data(msg), capture->last_payload_size);
    }
    return EV_OK;
}

static void publish_lease_rx(ev_command_actor_ctx_t *actor, const char *topic, const char *payload)
{
    ev_msg_t msg = {0};
    test_payload_lease_state_t state;
    ev_net_payload_lease_t lease;

    lease = test_payload_make_lease(&state, topic, payload);
    assert(ev_msg_init_publish(&msg, EV_NET_MQTT_MSG_RX_LEASE, ACT_RUNTIME) == EV_OK);
    assert(ev_msg_set_external_payload(&msg,
                                       lease.data,
                                       lease.size,
                                       lease.retain_fn,
                                       lease.release_fn,
                                       lease.lifecycle_ctx) == EV_OK);
    assert(ev_command_actor_handle(actor, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(state.retain_count == 1U);
    assert(state.release_count == 1U);
    assert(state.in_use == false);
}

static void tick(ev_command_actor_ctx_t *actor)
{
    ev_msg_t msg = {0};
    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_RUNTIME) == EV_OK);
    assert(ev_command_actor_handle(actor, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_empty_and_wrong_token_rejects(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "", EV_COMMAND_CAP_LED) == EV_OK);
    publish_lease_rx(&actor, "cmd/led", "token=x;mask=1;valid=1");
    assert(capture.calls == 0U);
    assert(actor.stats.disabled_rejects == 1U);

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_LED) == EV_OK);
    publish_lease_rx(&actor, "cmd/led", "token=bad;mask=1;valid=1");
    assert(capture.calls == 0U);
    assert(actor.stats.auth_rejects == 1U);
}

static void test_unknown_and_capability_rejects(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", 0U) == EV_OK);
    publish_lease_rx(&actor, "cmd/led", "token=secret;mask=1;valid=1");
    assert(capture.calls == 0U);
    assert(actor.stats.capability_rejects == 1U);

    publish_lease_rx(&actor, "cmd/xxx", "token=secret");
    assert(capture.calls == 0U);
    assert(actor.stats.unknown_topic == 1U);
}

static void test_led_command_and_rate_limit(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    const ev_panel_led_set_cmd_t *cmd;

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_LED) == EV_OK);
    publish_lease_rx(&actor, "cmd/led", "token=secret;mask=3;valid=f");
    assert(capture.calls == 1U);
    assert(capture.last_event == EV_PANEL_LED_SET_CMD);
    assert(capture.last_target == ACT_MCP23008);
    assert(capture.last_payload_size == sizeof(ev_panel_led_set_cmd_t));
    cmd = (const ev_panel_led_set_cmd_t *)capture.last_payload;
    assert(cmd->value_mask == 3U);
    assert(cmd->valid_mask == EV_MCP23008_LED_MASK);
    assert(actor.stats.commands_executed == 1U);
    assert(actor.stats.led_commands == 1U);

    publish_lease_rx(&actor, "cmd/led", "token=secret;mask=1;valid=1");
    assert(capture.calls == 1U);
    assert(actor.stats.rate_limited == 1U);
}

static void test_display_command(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    const ev_oled_display_text_cmd_t *cmd;

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_DISPLAY) == EV_OK);
    publish_lease_rx(&actor, "cmd/display", "token=secret;text=HELLO");
    assert(capture.calls == 1U);
    assert(capture.last_event == EV_OLED_DISPLAY_TEXT_CMD);
    assert(capture.last_target == ACT_OLED);
    assert(capture.last_payload_size == sizeof(ev_oled_display_text_cmd_t));
    cmd = (const ev_oled_display_text_cmd_t *)capture.last_payload;
    assert(cmd->page == EV_COMMAND_DISPLAY_PAGE);
    assert(cmd->column == EV_COMMAND_DISPLAY_COLUMN);
    assert(memcmp(cmd->text, "HELLO", 6U) == 0);
}

static void test_sleep_two_step(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    const ev_sys_goto_sleep_cmd_t *cmd;

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_SLEEP) == EV_OK);
    publish_lease_rx(&actor, "cmd/sleep", "token=secret;arm=ab;ms=100");
    assert(capture.calls == 0U);
    assert(actor.stats.sleep_arm_requests == 1U);

    publish_lease_rx(&actor, "cmd/sleep", "token=secret;confirm=bad");
    assert(capture.calls == 0U);
    assert(actor.stats.sleep_confirm_mismatch == 1U);

    publish_lease_rx(&actor, "cmd/sleep", "token=secret;confirm=ab");
    assert(capture.calls == 1U);
    assert(capture.last_event == EV_SYS_GOTO_SLEEP_CMD);
    assert(capture.last_target == ACT_POWER);
    assert(capture.last_payload_size == sizeof(ev_sys_goto_sleep_cmd_t));
    cmd = (const ev_sys_goto_sleep_cmd_t *)capture.last_payload;
    assert(cmd->duration_ms == 100U);
    assert(actor.stats.sleep_commands == 1U);
}

static void test_sleep_expiry(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    size_t i;

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_SLEEP) == EV_OK);
    publish_lease_rx(&actor, "cmd/sleep", "token=secret;arm=ab;ms=100");
    for (i = 0U; i < (EV_COMMAND_SLEEP_CONFIRM_WINDOW_TICKS + 1U); ++i) {
        tick(&actor);
    }
    publish_lease_rx(&actor, "cmd/sleep", "token=secret;confirm=ab");
    assert(capture.calls == 0U);
    assert(actor.stats.sleep_expired >= 1U);
}

static void test_lease_command_releases(void)
{
    ev_command_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    ev_msg_t msg = {0};
    test_payload_lease_state_t lease_state;
    ev_net_payload_lease_t lease;
    ev_net_mqtt_rx_payload_t *rx;

    lease = test_payload_make_lease(&lease_state, "cmd/display", "token=secret;text=LEASED");
    rx = &lease_state.payload;

    assert(ev_command_actor_init(&actor, capture_deliver, &capture, "secret", EV_COMMAND_CAP_DISPLAY) == EV_OK);
    assert(ev_msg_init_publish(&msg, EV_NET_MQTT_MSG_RX_LEASE, ACT_RUNTIME) == EV_OK);
    assert(ev_msg_set_external_payload(&msg,
                                       lease.data,
                                       lease.size,
                                       lease.retain_fn,
                                       lease.release_fn,
                                       lease.lifecycle_ctx) == EV_OK);
    assert(ev_command_actor_handle(&actor, &msg) == EV_OK);
    assert(capture.calls == 1U);
    assert(capture.last_event == EV_OLED_DISPLAY_TEXT_CMD);
    assert(rx->payload_len > 0U);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(lease_state.retain_count == 1U);
    assert(lease_state.release_count == 1U);
    assert(lease_state.in_use == false);
}

int main(void)
{
    test_empty_and_wrong_token_rejects();
    test_unknown_and_capability_rejects();
    test_led_command_and_rate_limit();
    test_display_command();
    test_sleep_two_step();
    test_sleep_expiry();
    test_lease_command_releases();
    return 0;
}
