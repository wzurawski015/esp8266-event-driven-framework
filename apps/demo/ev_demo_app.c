#include "ev/demo_app.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/capabilities.h"
#include "ev/compiler.h"
#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/runtime_poll.h"
#include "ev/demo_runtime_instances.h"

#define EV_DEMO_APP_DEFAULT_TICK_MS 1000U
#define EV_DEMO_APP_FAST_TICK_MS 100U
#define EV_DEMO_APP_TURN_BUDGET 4U
#define EV_APP_POLL_MAX_IRQ_SAMPLES 16U
#define EV_APP_POLL_RESERVED_IRQ_SAMPLES 4U
#define EV_APP_POLL_MAX_NET_SAMPLES 16U
#define EV_APP_POLL_RESERVED_NET_SAMPLES 4U
#define EV_APP_POLL_MAX_MESSAGES 32U
#define EV_APP_POLL_MAX_PUMP_TURNS 10U
#define EV_DEMO_APP_BUTTON_TOGGLE_SCREENSAVER 0U
#define EV_DEMO_APP_LED_SCREENSAVER_PAUSED 0x01U
#define EV_DEMO_APP_OLED_TITLE_TEXT "ATNEL AIR"
#define EV_DEMO_APP_OLED_TITLE_PAGES 3U
#define EV_DEMO_APP_OLED_TITLE_PAGE_OFFSET 0U
#define EV_DEMO_APP_OLED_TIME_PAGE_OFFSET 1U
#define EV_DEMO_APP_OLED_TEMP_PAGE_OFFSET 2U
#define EV_DEMO_APP_OLED_TEXT_CELL_ADVANCE 6U
#define EV_DEMO_APP_OLED_BLOCK_WIDTH_PX ((uint8_t)((sizeof(EV_DEMO_APP_OLED_TITLE_TEXT) - 1U) * EV_DEMO_APP_OLED_TEXT_CELL_ADVANCE))
#define EV_DEMO_APP_OLED_MAX_COLUMN_OFFSET ((uint8_t)(EV_OLED_WIDTH - EV_DEMO_APP_OLED_BLOCK_WIDTH_PX))
#define EV_DEMO_APP_OLED_MAX_PAGE_OFFSET ((uint8_t)(EV_OLED_PAGE_COUNT - EV_DEMO_APP_OLED_TITLE_PAGES))
#define EV_DEMO_APP_MIN_WDT_TIMEOUT_MS 1000U
#define EV_DEMO_APP_MAX_WDT_TIMEOUT_MS 60000U

EV_STATIC_ASSERT(EV_DEMO_APP_OLED_BLOCK_WIDTH_PX <= EV_OLED_WIDTH, "OLED block width must fit the panel");
EV_STATIC_ASSERT(EV_DEMO_APP_OLED_TITLE_PAGES <= EV_OLED_PAGE_COUNT, "OLED block height must fit the panel");

static const ev_demo_app_board_profile_t k_ev_demo_app_default_board_profile = {
    .capabilities_mask = 0U,
    .hardware_present_mask = 0U,
    .supervisor_required_mask = 0U,
    .supervisor_optional_mask = 0U,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = 0U,
    .mcp23008_addr_7bit = 0U,
    .rtc_addr_7bit = 0U,
    .oled_addr_7bit = 0U,
    .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    .watchdog_timeout_ms = 0U,
    .remote_command_token = "",
    .remote_command_capabilities = 0U,
};

const ev_demo_app_board_profile_t *ev_demo_app_default_board_profile(void)
{
    return &k_ev_demo_app_default_board_profile;
}

static bool ev_demo_app_hw_mask_valid(uint32_t hw_mask)
{
    return (hw_mask & (uint32_t)(~EV_SUPERVISOR_KNOWN_MASK)) == 0U;
}

static bool ev_demo_app_profile_is_valid(const ev_demo_app_board_profile_t *profile)
{
    uint32_t supervised_mask;

    if (profile == NULL) {
        return false;
    }

    supervised_mask = profile->supervisor_required_mask | profile->supervisor_optional_mask;
    if (!ev_demo_app_hw_mask_valid(profile->hardware_present_mask) ||
        !ev_demo_app_hw_mask_valid(profile->supervisor_required_mask) ||
        !ev_demo_app_hw_mask_valid(profile->supervisor_optional_mask)) {
        return false;
    }
    if ((profile->supervisor_required_mask & profile->supervisor_optional_mask) != 0U) {
        return false;
    }
    if ((supervised_mask & (uint32_t)(~profile->hardware_present_mask)) != 0U) {
        return false;
    }
    if ((profile->hardware_present_mask & (EV_SUPERVISOR_HW_MCP23008 |
                                           EV_SUPERVISOR_HW_RTC |
                                           EV_SUPERVISOR_HW_OLED)) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_I2C0) == 0U) {
            return false;
        }
    }
    if ((profile->hardware_present_mask & EV_SUPERVISOR_HW_DS18B20) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_ONEWIRE0) == 0U) {
            return false;
        }
    }
    if ((profile->hardware_present_mask & EV_SUPERVISOR_HW_RTC) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_GPIO_IRQ) == 0U) {
            return false;
        }
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_MCP23008) != 0U) &&
        ((profile->mcp23008_addr_7bit == 0U) || (profile->mcp23008_addr_7bit > 0x7FU))) {
        return false;
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_RTC) != 0U) &&
        ((profile->rtc_addr_7bit == 0U) || (profile->rtc_addr_7bit > 0x7FU))) {
        return false;
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_OLED) != 0U) &&
        ((profile->oled_addr_7bit == 0U) || (profile->oled_addr_7bit > 0x7FU))) {
        return false;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        if ((profile->watchdog_timeout_ms < EV_DEMO_APP_MIN_WDT_TIMEOUT_MS) ||
            (profile->watchdog_timeout_ms > EV_DEMO_APP_MAX_WDT_TIMEOUT_MS)) {
            return false;
        }
    }

    return true;
}

typedef struct {
    uint32_t sequence;
    uint32_t ticks_seen;
    uint32_t last_tick_ms;
    uint32_t boot_completions;
} ev_demo_snapshot_t;

EV_STATIC_ASSERT(sizeof(ev_demo_snapshot_t) == EV_DEMO_APP_SNAPSHOT_BYTES, "demo snapshot ABI mismatch");
EV_STATIC_ASSERT(sizeof(ev_oled_scene_t) <= EV_DEMO_APP_LEASE_SLOT_BYTES,
                 "OLED scene payload must fit inside one demo lease slot");

static void ev_demo_app_logf(ev_demo_app_t *app, ev_log_level_t level, const char *fmt, ...)
{
    char buffer[192];
    va_list ap;
    int len;

    if ((app == NULL) || (app->log_port == NULL) || (app->log_port->write == NULL) || (app->app_tag == NULL) ||
        (fmt == NULL)) {
        return;
    }

    va_start(ap, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(buffer)) {
        len = (int)(sizeof(buffer) - 1U);
        buffer[len] = '\0';
    }

    (void)app->log_port->write(app->log_port->ctx, level, app->app_tag, buffer, (size_t)len);
}

static ev_result_t ev_demo_app_now_ms(ev_demo_app_t *app, uint32_t *out_now_ms)
{
    ev_time_mono_us_t now_us;
    ev_result_t rc;

    if ((app == NULL) || (out_now_ms == NULL) || (app->clock_port == NULL) || (app->clock_port->mono_now_us == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = app->clock_port->mono_now_us(app->clock_port->ctx, &now_us);
    if (rc != EV_OK) {
        return rc;
    }

    *out_now_ms = (uint32_t)(now_us / 1000ULL);
    return EV_OK;
}

static ev_result_t ev_demo_app_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context);
static ev_result_t ev_demo_app_publish_net_event(ev_demo_app_t *app, const ev_net_ingress_event_t *event);

static void ev_demo_app_record_delivery_report(ev_demo_app_t *app, const ev_delivery_report_t *report)
{
    if ((app == NULL) || (report == NULL)) {
        return;
    }
    app->stats.disabled_route_deliveries += (uint32_t)report->optional_disabled_routes;
    app->stats.watchdog_disabled_route_deliveries += (uint32_t)report->optional_disabled_watchdog_routes;
    app->stats.network_disabled_route_deliveries += (uint32_t)report->optional_disabled_network_routes;
}

static ev_result_t ev_demo_app_publish_owned(ev_demo_app_t *app, ev_msg_t *msg)
{
    ev_result_t rc;
    ev_result_t dispose_rc;
    ev_delivery_report_t report;

    if ((app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_runtime_graph_publish(&app->graph, msg, &report);
    ev_demo_app_record_delivery_report(app, &report);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
    }

    dispose_rc = ev_msg_dispose(msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        return dispose_rc;
    }

    return rc;
}

static bool ev_demo_app_hardware_active(const ev_demo_app_actor_state_t *state, uint32_t hw_mask);

static ev_result_t ev_demo_app_publish_snapshot(ev_demo_diag_actor_state_t *state)
{
    ev_demo_app_t *app;
    ev_lease_handle_t handle = {0};
    ev_demo_snapshot_t *snapshot = NULL;
    ev_msg_t msg = {0};
    void *data = NULL;
    ev_result_t rc;

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    rc = ev_lease_pool_acquire(&app->lease_pool, sizeof(*snapshot), &handle, &data);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
        ev_demo_app_logf(app, EV_LOG_ERROR, "snapshot acquire failed rc=%d", (int)rc);
        return rc;
    }

    snapshot = (ev_demo_snapshot_t *)data;
    snapshot->sequence = state->snapshots_sent + 1U;
    snapshot->ticks_seen = state->ticks_seen;
    snapshot->last_tick_ms = state->last_tick_ms;
    snapshot->boot_completions = app->stats.boot_completions;

    rc = ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG);
    if (rc == EV_OK) {
        rc = ev_lease_pool_attach_msg(&msg, &handle);
    }
    if (rc == EV_OK) {
        rc = ev_demo_app_publish_owned(app, &msg);
    } else {
        ++app->stats.publish_errors;
        (void)ev_msg_dispose(&msg);
    }

    (void)ev_lease_pool_release(&handle);
    if (rc != EV_OK) {
        return rc;
    }

    ++state->snapshots_sent;
    ++app->stats.snapshots_published;
    return EV_OK;
}

static ev_result_t ev_demo_app_publish_diag_request(ev_demo_app_actor_state_t *state)
{
    ev_demo_app_t *app;
    ev_msg_t msg = {0};
    ev_result_t rc;

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    rc = ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP);
    if (rc != EV_OK) {
        return rc;
    }

    return ev_demo_app_publish_owned(app, &msg);
}

static ev_result_t ev_demo_app_publish_system_event(ev_demo_app_t *app,
                                                 ev_event_id_t event_id,
                                                 const void *payload,
                                                 size_t payload_size)
{
    ev_msg_t msg = {0};
    ev_result_t rc;

    if ((app == NULL) || ((payload == NULL) && (payload_size != 0U))) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, event_id, ACT_BOOT);
    if (rc != EV_OK) {
        return rc;
    }

    if (payload_size > 0U) {
        rc = ev_msg_set_inline_payload(&msg, payload, payload_size);
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            return rc;
        }
    }

    return ev_demo_app_publish_owned(app, &msg);
}

static ev_result_t ev_demo_app_publish_tick(ev_demo_app_t *app)
{
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_demo_app_publish_system_event(app, EV_TICK_1S, NULL, 0U);
    if (rc == EV_OK) {
        ++app->stats.ticks_published;
    }

    return rc;
}

static ev_result_t ev_demo_app_publish_tick_100ms(ev_demo_app_t *app)
{
    return ev_demo_app_publish_system_event(app, EV_TICK_100MS, NULL, 0U);
}

static ev_result_t ev_demo_app_publish_panel_led_command(ev_demo_app_t *app, uint8_t value_mask, uint8_t valid_mask)
{
    ev_msg_t msg = {0};
    ev_panel_led_set_cmd_t cmd = {0};
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    cmd.value_mask = (uint8_t)(value_mask & EV_MCP23008_LED_MASK);
    cmd.valid_mask = (uint8_t)(valid_mask & EV_MCP23008_LED_MASK);

    rc = ev_msg_init_publish(&msg, EV_PANEL_LED_SET_CMD, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, &cmd, sizeof(cmd));
    }
    if (rc != EV_OK) {
        (void)ev_msg_dispose(&msg);
        return rc;
    }

    return ev_demo_app_publish_owned(app, &msg);
}

static bool ev_demo_app_hardware_active(const ev_demo_app_actor_state_t *state, uint32_t hw_mask)
{
    return (state != NULL) && state->system_ready && ((state->active_hardware_mask & hw_mask) != 0U);
}

static ev_result_t ev_demo_app_publish_irq_sample(ev_demo_app_t *app, const ev_irq_sample_t *sample)
{
    if ((app == NULL) || (sample == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    return ev_demo_app_publish_system_event(app, EV_GPIO_IRQ, sample, sizeof(*sample));
}


static ev_result_t ev_demo_app_publish_oled_scene_commit(ev_demo_app_t *app, const ev_oled_scene_t *scene)
{
    ev_lease_handle_t handle = {0};
    ev_msg_t msg = {0};
    void *data = NULL;
    ev_result_t rc;

    if ((app == NULL) || (scene == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_lease_pool_acquire(&app->lease_pool, sizeof(*scene), &handle, &data);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
        ev_demo_app_logf(app, EV_LOG_ERROR, "oled scene acquire failed rc=%d", (int)rc);
        return rc;
    }

    memcpy(data, scene, sizeof(*scene));

    rc = ev_msg_init_publish(&msg, EV_OLED_COMMIT_FRAME, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_lease_pool_attach_msg(&msg, &handle);
    }
    if (rc == EV_OK) {
        rc = ev_demo_app_publish_owned(app, &msg);
    } else {
        ++app->stats.publish_errors;
        (void)ev_msg_dispose(&msg);
    }

    (void)ev_lease_pool_release(&handle);
    return rc;
}

static void ev_demo_app_format_time_text(const ev_demo_app_actor_state_t *state, char *out_text, size_t out_text_size)
{
    if ((state == NULL) || (out_text == NULL) || (out_text_size == 0U)) {
        return;
    }

    if (!state->time_valid) {
        (void)snprintf(out_text, out_text_size, "--:--:--");
        return;
    }

    (void)snprintf(out_text,
                   out_text_size,
                   "%02u:%02u:%02u",
                   (unsigned)state->last_time.hours,
                   (unsigned)state->last_time.minutes,
                   (unsigned)state->last_time.seconds);
}

static void ev_demo_app_format_temp_text(const ev_demo_app_actor_state_t *state, char *out_text, size_t out_text_size)
{
    int32_t centi_celsius;
    uint32_t abs_centi_celsius;
    const char *sign;

    if ((state == NULL) || (out_text == NULL) || (out_text_size == 0U)) {
        return;
    }

    if (!state->temp_valid) {
        (void)snprintf(out_text, out_text_size, "--.-- C");
        return;
    }

    centi_celsius = (int32_t)state->last_temp.centi_celsius;
    abs_centi_celsius = (centi_celsius < 0) ? (uint32_t)(-centi_celsius) : (uint32_t)centi_celsius;
    sign = (centi_celsius < 0) ? "-" : "";

    (void)snprintf(out_text,
                   out_text_size,
                   "%s%lu.%02lu C",
                   sign,
                   (unsigned long)(abs_centi_celsius / 100U),
                   (unsigned long)(abs_centi_celsius % 100U));
}


static ev_result_t ev_demo_app_render_oled_frame(ev_demo_app_actor_state_t *state)
{
    ev_demo_app_t *app;
    char time_text[EV_OLED_TEXT_MAX_CHARS] = {0};
    char temp_text[EV_OLED_TEXT_MAX_CHARS] = {0};

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    if (!ev_demo_app_hardware_active(state, EV_SUPERVISOR_HW_OLED)) {
        return EV_OK;
    }
    ev_demo_app_format_time_text(state, time_text, sizeof(time_text));
    ev_demo_app_format_temp_text(state, temp_text, sizeof(temp_text));

    memset(&state->oled_scene, 0, sizeof(state->oled_scene));
    state->oled_scene.page_offset = state->current_page_offset;
    state->oled_scene.column_offset = state->current_column_offset;
    state->oled_scene.flags = EV_OLED_SCENE_FLAG_VISIBLE;
    (void)snprintf(state->oled_scene.lines[0], sizeof(state->oled_scene.lines[0]), "%s", EV_DEMO_APP_OLED_TITLE_TEXT);
    (void)snprintf(state->oled_scene.lines[1], sizeof(state->oled_scene.lines[1]), "%s", time_text);
    (void)snprintf(state->oled_scene.lines[2], sizeof(state->oled_scene.lines[2]), "%s", temp_text);

    state->last_page_offset = state->current_page_offset;
    state->last_column_offset = state->current_column_offset;
    state->oled_frame_visible = true;
    return ev_demo_app_publish_oled_scene_commit(app, &state->oled_scene);
}

static void ev_demo_app_screensaver_step_axis(uint8_t *value, int8_t *direction, uint8_t max_value)
{
    if ((value == NULL) || (direction == NULL)) {
        return;
    }

    if (max_value == 0U) {
        *value = 0U;
        *direction = (int8_t)1;
        return;
    }

    if (*direction >= 0) {
        if (*value >= max_value) {
            *direction = (int8_t)-1;
            if (*value > 0U) {
                --(*value);
            }
        } else {
            ++(*value);
        }
    } else if (*value == 0U) {
        *direction = (int8_t)1;
        ++(*value);
    } else {
        --(*value);
    }
}

static ev_result_t ev_demo_app_handle_tick_for_oled(ev_demo_app_actor_state_t *state)
{
    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    if (!state->screensaver_paused) {
        ev_demo_app_screensaver_step_axis(&state->current_column_offset,
                                          &state->direction_x,
                                          EV_DEMO_APP_OLED_MAX_COLUMN_OFFSET);
        ev_demo_app_screensaver_step_axis(&state->current_page_offset,
                                          &state->direction_y,
                                          EV_DEMO_APP_OLED_MAX_PAGE_OFFSET);
    }

    return ev_demo_app_render_oled_frame(state);
}

ev_result_t ev_demo_app_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    const ev_demo_snapshot_t *snapshot;
    ev_demo_app_actor_state_t *state = (ev_demo_app_actor_state_t *)actor_context;
    ev_demo_app_t *app;

    if ((state == NULL) || (state->app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        ++app->stats.boot_completions;
        state->current_page_offset = 0U;
        state->current_column_offset = 0U;
        state->last_page_offset = 0U;
        state->last_column_offset = 0U;
        state->direction_x = (int8_t)1;
        state->direction_y = (int8_t)1;
        state->oled_frame_visible = false;
        state->screensaver_paused = false;
        state->panel_led_mask = 0U;
        state->system_ready = false;
        state->active_hardware_mask = 0U;
        return EV_OK;

    case EV_SYSTEM_READY:
        {
            const ev_system_ready_payload_t *ready_payload = (const ev_system_ready_payload_t *)ev_msg_payload_data(msg);
            ev_result_t rc;
            bool first_system_ready;

            if ((ready_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*ready_payload))) {
                return EV_ERR_CONTRACT;
            }

            first_system_ready = !state->system_ready;
            state->system_ready = true;
            state->active_hardware_mask = ready_payload->active_hardware_mask;
            state->temp_valid = (state->temp_valid && ((state->active_hardware_mask & EV_SUPERVISOR_HW_DS18B20) != 0U));
            ev_demo_app_logf(app, EV_LOG_INFO, "app actor: system ready hw_mask=0x%08lx", (unsigned long)state->active_hardware_mask);

            if (first_system_ready) {
                if ((state->active_hardware_mask & EV_SUPERVISOR_HW_MCP23008) != 0U) {
                    rc = ev_demo_app_publish_panel_led_command(app, 0U, EV_MCP23008_LED_MASK);
                    if (rc != EV_OK) {
                        return rc;
                    }
                }

                rc = ev_demo_app_publish_diag_request(state);
                if (rc != EV_OK) {
                    return rc;
                }
            }

            return ev_demo_app_render_oled_frame(state);
        }

    case EV_TICK_1S:
        if (!state->system_ready) {
            return EV_OK;
        }
        return ev_demo_app_handle_tick_for_oled(state);

    case EV_TEMP_UPDATED:
        {
            const ev_temp_payload_t *temp_payload = (const ev_temp_payload_t *)ev_msg_payload_data(msg);

            if ((temp_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*temp_payload))) {
                return EV_ERR_CONTRACT;
            }

            state->last_temp = *temp_payload;
            state->temp_valid = true;
            return EV_OK;
        }

    case EV_TIME_UPDATED:
        {
            const ev_time_payload_t *time_payload = (const ev_time_payload_t *)ev_msg_payload_data(msg);

            if ((time_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*time_payload))) {
                return EV_ERR_CONTRACT;
            }

            state->last_time = *time_payload;
            state->time_valid = true;
            return EV_OK;
        }

    case EV_BUTTON_EVENT:
        {
            const ev_button_event_payload_t *button_payload =
                (const ev_button_event_payload_t *)ev_msg_payload_data(msg);

            if ((button_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*button_payload))) {
                return EV_ERR_CONTRACT;
            }

            if ((button_payload->button_id == EV_DEMO_APP_BUTTON_TOGGLE_SCREENSAVER) &&
                (button_payload->action == EV_BUTTON_ACTION_SHORT)) {
                ev_result_t rc;

                state->screensaver_paused = !state->screensaver_paused;
                if (state->screensaver_paused) {
                    state->panel_led_mask = (uint8_t)(state->panel_led_mask | EV_DEMO_APP_LED_SCREENSAVER_PAUSED);
                } else {
                    state->panel_led_mask =
                        (uint8_t)(state->panel_led_mask & (uint8_t)(~EV_DEMO_APP_LED_SCREENSAVER_PAUSED));
                }

                rc = ev_demo_app_publish_panel_led_command(app,
                                                           state->panel_led_mask,
                                                           EV_DEMO_APP_LED_SCREENSAVER_PAUSED);
                if (rc != EV_OK) {
                    return rc;
                }

                ev_demo_app_logf(app,
                                 EV_LOG_INFO,
                                 "app actor: screensaver %s",
                                 state->screensaver_paused ? "paused" : "resumed");
            }

            return EV_OK;
        }

    case EV_DIAG_SNAPSHOT_RSP:
        snapshot = (const ev_demo_snapshot_t *)ev_msg_payload_data(msg);
        if ((snapshot == NULL) || (ev_msg_payload_size(msg) != sizeof(*snapshot))) {
            return EV_ERR_CONTRACT;
        }
        state->last_snapshot_sequence = snapshot->sequence;
        state->last_diag_ticks_seen = snapshot->ticks_seen;
        ++app->stats.snapshots_received;
        ev_demo_app_logf(app,
                         EV_LOG_INFO,
                         "app actor: snapshot seq=%u diag_ticks=%u last_tick_ms=%u",
                         (unsigned)snapshot->sequence,
                         (unsigned)snapshot->ticks_seen,
                         (unsigned)snapshot->last_tick_ms);
        return EV_OK;

    default:
        return EV_ERR_CONTRACT;
    }
}

ev_result_t ev_demo_diag_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_demo_diag_actor_state_t *state = (ev_demo_diag_actor_state_t *)actor_context;
    ev_demo_app_t *app;
    ev_result_t rc;
    uint32_t now_ms = 0U;

    if ((state == NULL) || (state->app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;

    switch (msg->event_id) {
    case EV_BOOT_STARTED:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: observed boot start on %s", app->board_name);
        return EV_OK;

    case EV_BOOT_COMPLETED:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: observed boot completion");
        return EV_OK;

    case EV_TICK_1S:
        rc = ev_demo_app_now_ms(app, &now_ms);
        if (rc != EV_OK) {
            return rc;
        }
        state->last_tick_ms = now_ms;
        ++state->ticks_seen;
        ++app->stats.diag_ticks_seen;
        ev_demo_app_logf(app,
                         EV_LOG_INFO,
                         "diag actor: tick=%u mono_now_ms=%u",
                         (unsigned)state->ticks_seen,
                         (unsigned)state->last_tick_ms);


        return ev_demo_app_publish_snapshot(state);

    case EV_TICK_100MS:
        return EV_OK;

    case EV_MCP23008_READY:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: mcp23008 ready -> rtc may arm sqw irq");
        return EV_OK;

    case EV_GPIO_IRQ:
        {
            const ev_irq_sample_t *sample = (const ev_irq_sample_t *)ev_msg_payload_data(msg);
            const char *edge_name;

            if ((sample == NULL) || (ev_msg_payload_size(msg) != sizeof(*sample))) {
                return EV_ERR_CONTRACT;
            }

            if (sample->line_id == app->board_profile.rtc_sqw_line_id) {
                edge_name = (sample->edge == EV_IRQ_EDGE_FALLING) ? "falling" :
                            ((sample->edge == EV_IRQ_EDGE_RISING) ? "rising" : "unknown");
                ++state->rtc_irq_samples_seen;
                ev_demo_app_logf(app,
                                 EV_LOG_INFO,
                                 "diag actor: rtc irq count=%u line=%u edge=%s level=%u ts_us=%lu",
                                 (unsigned)state->rtc_irq_samples_seen,
                                 (unsigned)sample->line_id,
                                 edge_name,
                                 (unsigned)sample->level,
                                 (unsigned long)sample->timestamp_us);
            }

            return EV_OK;
        }

    case EV_DIAG_SNAPSHOT_REQ:
        return ev_demo_app_publish_snapshot(state);

    default:
        return EV_ERR_CONTRACT;
    }
}

typedef struct {
    size_t irq_samples;
    size_t net_samples;
    size_t pump_calls;
    size_t turns;
    size_t messages;
} ev_demo_app_poll_diag_t;

typedef struct {
    size_t pump_calls_used;
    size_t messages_used;
    size_t turns_used;
    size_t irq_samples_used;
    size_t net_samples_used;
    bool exhausted;
} ev_poll_budget_t;

static void ev_demo_app_poll_diag_reset(ev_demo_app_poll_diag_t *diag)
{
    if (diag != NULL) {
        memset(diag, 0, sizeof(*diag));
    }
}

static void ev_demo_app_record_poll_diag(ev_demo_app_t *app,
                                         const ev_demo_app_poll_diag_t *diag,
                                         size_t pending_before,
                                         size_t pending_after,
                                         uint32_t elapsed_ms)
{
    if ((app == NULL) || (diag == NULL)) {
        return;
    }

    app->stats.irq_samples_drained += (uint32_t)diag->irq_samples;
    if (pending_before > app->stats.max_pending_before_poll) {
        app->stats.max_pending_before_poll = pending_before;
    }
    if (pending_after > app->stats.max_pending_after_poll) {
        app->stats.max_pending_after_poll = pending_after;
    }
    if (diag->irq_samples > app->stats.max_irq_samples_per_poll) {
        app->stats.max_irq_samples_per_poll = diag->irq_samples;
    }
    if (diag->net_samples > app->stats.max_net_samples_per_poll) {
        app->stats.max_net_samples_per_poll = diag->net_samples;
    }
    if (diag->pump_calls > app->stats.max_pump_calls_per_poll) {
        app->stats.max_pump_calls_per_poll = diag->pump_calls;
    }
    if (diag->turns > app->stats.max_turns_per_poll) {
        app->stats.max_turns_per_poll = diag->turns;
    }
    if (diag->messages > app->stats.max_messages_per_poll) {
        app->stats.max_messages_per_poll = diag->messages;
    }
    app->stats.last_poll_elapsed_ms = elapsed_ms;
    if (elapsed_ms > app->stats.max_poll_elapsed_ms) {
        app->stats.max_poll_elapsed_ms = elapsed_ms;
    }
}

static void ev_demo_app_record_irq_stats(ev_demo_app_t *app)
{
    ev_irq_stats_t irq_stats = {0};

    if ((app == NULL) || (app->irq_port == NULL) || (app->irq_port->get_stats == NULL)) {
        return;
    }
    if (app->irq_port->get_stats(app->irq_port->ctx, &irq_stats) != EV_OK) {
        return;
    }

    app->stats.irq_samples_dropped_observed = irq_stats.dropped_samples;
    if (irq_stats.pending_samples > app->stats.irq_samples_pending_high_watermark) {
        app->stats.irq_samples_pending_high_watermark = irq_stats.pending_samples;
    }
    if (irq_stats.high_watermark > app->stats.irq_ring_high_watermark_observed) {
        app->stats.irq_ring_high_watermark_observed = irq_stats.high_watermark;
    }
}

static void ev_demo_app_record_net_stats(ev_demo_app_t *app)
{
    ev_net_stats_t stats;

    if ((app == NULL) || (app->net_port == NULL) || (app->net_port->get_stats == NULL)) {
        return;
    }
    if (app->net_port->get_stats(app->net_port->ctx, &stats) != EV_OK) {
        return;
    }
    if (stats.dropped_events > app->stats.net_events_dropped_observed) {
        app->stats.net_events_dropped_observed = stats.dropped_events;
    }
    if (stats.dropped_oversize > app->stats.net_payload_dropped_oversize) {
        app->stats.net_payload_dropped_oversize = stats.dropped_oversize;
    }
    if (stats.dropped_no_payload_slot > app->stats.net_no_payload_slot_drops_observed) {
        app->stats.net_no_payload_slot_drops_observed = stats.dropped_no_payload_slot;
    }
    if (stats.high_watermark > app->stats.net_ring_high_watermark_observed) {
        app->stats.net_ring_high_watermark_observed = stats.high_watermark;
    }
}

static bool ev_demo_app_profile_has_hardware(const ev_demo_app_t *app, uint32_t hw_mask)
{
    return (app != NULL) && ((app->board_profile.hardware_present_mask & hw_mask) != 0U);
}

static const ev_active_route_t *ev_demo_app_find_active_route(const ev_demo_app_t *app,
                                                               ev_event_id_t event_id,
                                                               ev_actor_id_t target_actor)
{
    const ev_active_route_table_t *routes;
    size_t i;

    if (app == NULL) {
        return NULL;
    }
    routes = ev_runtime_graph_active_routes(&app->graph);
    if (routes == NULL) {
        return NULL;
    }
    for (i = 0U; i < routes->count; ++i) {
        const ev_active_route_t *entry = ev_active_route_at(routes, i);
        if ((entry != NULL) &&
            (entry->route.event_id == event_id) &&
            (entry->route.target_actor == target_actor)) {
            return entry;
        }
    }
    return NULL;
}

static void ev_demo_app_record_disabled_route(ev_demo_app_t *app, ev_actor_id_t target_actor)
{
    if (app == NULL) {
        return;
    }
    ++app->stats.disabled_route_deliveries;
    if (target_actor == ACT_WATCHDOG) {
        ++app->stats.watchdog_disabled_route_deliveries;
    }
    if (target_actor == ACT_NETWORK) {
        ++app->stats.network_disabled_route_deliveries;
    }
}

static ev_result_t ev_demo_app_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    ev_demo_app_t *app = (ev_demo_app_t *)context;
    const ev_active_route_t *route;

    if ((app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    route = ev_demo_app_find_active_route(app, msg->event_id, target_actor);
    if (route == NULL) {
        return EV_ERR_NOT_FOUND;
    }
    if (route->state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
        ev_demo_app_record_disabled_route(app, target_actor);
        return EV_OK;
    }
    if (route->state != EV_ACTIVE_ROUTE_ENABLED) {
        return (route->reason != EV_OK) ? route->reason : EV_ERR_STATE;
    }

    return ev_runtime_graph_send(&app->graph, target_actor, msg);
}

static bool ev_demo_app_i2c_port_valid(const ev_i2c_port_t *port)
{
    return (port != NULL) && (port->write_stream != NULL) &&
           (port->write_regs != NULL) && (port->read_regs != NULL);
}

static bool ev_demo_app_onewire_port_valid(const ev_onewire_port_t *port)
{
    return (port != NULL) && (port->reset != NULL) &&
           (port->write_byte != NULL) && (port->read_byte != NULL);
}

static bool ev_demo_app_irq_port_valid(const ev_irq_port_t *port)
{
    return (port != NULL) && (port->pop != NULL) && (port->enable != NULL);
}

static bool ev_demo_app_wdt_port_valid(const ev_wdt_port_t *port)
{
    return (port != NULL) && (port->enable != NULL) && (port->feed != NULL);
}

static bool ev_demo_app_net_port_valid(const ev_net_port_t *port)
{
    return (port != NULL) && (port->poll_ingress != NULL) &&
           (port->publish_mqtt != NULL) && (port->get_stats != NULL);
}

static bool ev_demo_app_config_is_valid(const ev_demo_app_config_t *cfg)
{
    const ev_demo_app_board_profile_t *profile;
    uint32_t hw_mask;

    if ((cfg == NULL) || (cfg->app_tag == NULL) || (cfg->board_name == NULL) ||
        (cfg->clock_port == NULL) || (cfg->clock_port->mono_now_us == NULL) ||
        (cfg->log_port == NULL) || (cfg->log_port->write == NULL) ||
        ((cfg->system_port != NULL) && (cfg->system_port->deep_sleep == NULL))) {
        return false;
    }

    profile = (cfg->board_profile != NULL) ? cfg->board_profile : ev_demo_app_default_board_profile();
    if (!ev_demo_app_profile_is_valid(profile)) {
        return false;
    }

    hw_mask = profile->hardware_present_mask;
    if ((hw_mask & (EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC | EV_SUPERVISOR_HW_OLED)) != 0U) {
        if (!ev_demo_app_i2c_port_valid(cfg->i2c_port)) {
            return false;
        }
    }
    if ((hw_mask & EV_SUPERVISOR_HW_DS18B20) != 0U) {
        if (!ev_demo_app_onewire_port_valid(cfg->onewire_port)) {
            return false;
        }
    }
    if ((hw_mask & EV_SUPERVISOR_HW_RTC) != 0U) {
        if (!ev_demo_app_irq_port_valid(cfg->irq_port)) {
            return false;
        }
    } else if ((cfg->irq_port != NULL) && ((cfg->irq_port->pop == NULL) || (cfg->irq_port->enable == NULL))) {
        return false;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        if (!ev_demo_app_wdt_port_valid(cfg->wdt_port)) {
            return false;
        }
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) {
        if (!ev_demo_app_net_port_valid(cfg->net_port)) {
            return false;
        }
    }

    return true;
}


static ev_capability_mask_t ev_demo_app_runtime_board_capabilities(const ev_demo_app_t *app)
{
    ev_capability_mask_t caps = EV_CAP_PANEL | EV_CAP_METRICS | EV_CAP_FAULTS |
                               EV_CAP_TRACE | EV_CAP_POWER_POLICY | EV_CAP_REMOTE_COMMANDS;
    if (app == NULL) {
        return caps;
    }
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_I2C0) != 0U) caps |= EV_CAP_I2C0;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_ONEWIRE0) != 0U) caps |= EV_CAP_ONEWIRE0;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_GPIO_IRQ) != 0U) caps |= EV_CAP_GPIO_IRQ;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) caps |= EV_CAP_WDT;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) caps |= EV_CAP_NET;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_RTC)) caps |= EV_CAP_RTC;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_OLED)) caps |= EV_CAP_OLED;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_DS18B20)) caps |= EV_CAP_DS18B20;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_MCP23008)) caps |= EV_CAP_MCP23008;
    return caps;
}

static ev_capability_mask_t ev_demo_app_runtime_capabilities(void)
{
    return EV_CAP_TIMERS | EV_CAP_METRICS | EV_CAP_FAULTS | EV_CAP_TRACE | EV_CAP_POWER_POLICY;
}

static ev_runtime_ports_t ev_demo_app_runtime_ports(ev_demo_app_t *app)
{
    ev_runtime_ports_t ports;
    memset(&ports, 0, sizeof(ports));
    if (app != NULL) {
        ports.clock = app->clock_port;
        ports.log = app->log_port;
        ports.irq = app->irq_port;
        ports.system = app->system_port;
        ports.wdt = app->wdt_port;
        ports.net = app->net_port;
    }
    return ports;
}

static ev_runtime_board_profile_t ev_demo_app_runtime_board_profile(const ev_demo_app_t *app)
{
    ev_runtime_board_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    if (app != NULL) {
        profile.board_name = app->board_name;
        profile.configured_capabilities = ev_demo_app_runtime_board_capabilities(app);
        profile.active_capabilities = profile.configured_capabilities;
        profile.hardware_present = profile.configured_capabilities;
        profile.required_hardware = app->board_profile.supervisor_required_mask;
        profile.optional_hardware = app->board_profile.supervisor_optional_mask;
        profile.bsp_private = &app->board_profile;
    }
    return profile;
}

static ev_result_t ev_demo_app_build_runtime_graph(ev_demo_app_t *app)
{
    ev_runtime_builder_t builder;
    ev_runtime_ports_t ports;
    ev_runtime_board_profile_t profile;
    ev_actor_instance_descriptor_t instances[EV_ACTOR_COUNT];
    size_t count = 0U;
    size_t i;
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_runtime_builder_init(&builder,
                                 &app->graph,
                                 ev_demo_app_runtime_board_capabilities(app),
                                 ev_demo_app_runtime_capabilities());
    if (rc != EV_OK) return rc;
    ports = ev_demo_app_runtime_ports(app);
    profile = ev_demo_app_runtime_board_profile(app);
    rc = ev_runtime_builder_set_ports(&builder, &ports);
    if (rc != EV_OK) return rc;
    rc = ev_runtime_builder_set_board_profile(&builder, &profile);
    if (rc != EV_OK) return rc;
    rc = ev_demo_runtime_instances_init(app, instances, EV_ACTOR_COUNT, &count);
    if (rc != EV_OK) return rc;
    for (i = 0U; i < count; ++i) {
        rc = ev_runtime_builder_add_instance(&builder, &instances[i]);
        if (rc != EV_OK) return rc;
    }
    rc = ev_runtime_builder_bind_routes(&builder);
    if (rc != EV_OK) return rc;
    return ev_runtime_builder_build(&builder);
}

static ev_result_t ev_demo_app_schedule_standard_timers(ev_demo_app_t *app, uint32_t now_ms)
{
    ev_result_t rc;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_timer_schedule_periodic(&app->graph.timer_service,
                                    now_ms,
                                    EV_DEMO_APP_FAST_TICK_MS,
                                    ACT_RUNTIME,
                                    EV_TICK_100MS,
                                    0U,
                                    &app->tick_100ms_token);
    if (rc != EV_OK) return rc;
    rc = ev_timer_schedule_periodic(&app->graph.timer_service,
                                    now_ms,
                                    app->tick_period_ms,
                                    ACT_RUNTIME,
                                    EV_TICK_1S,
                                    0U,
                                    &app->tick_1s_token);
    if (rc == EV_OK) {
        app->standard_timers_scheduled = true;
    }
    return rc;
}

static ev_result_t ev_demo_app_sleep_quiescence_guard(void *ctx,
                                                       uint64_t duration_us,
                                                       ev_power_quiescence_report_t *out_report)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    ev_power_quiescence_report_t report;
    ev_quiescence_report_t qreport;
    ev_quiescence_policy_t policy;
    bool irq_pending = false;
    uint32_t now_ms = 0U;
    ev_result_t rc;

    (void)duration_us;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    memset(&report, 0, sizeof(report));
    memset(&policy, 0, sizeof(policy));
    policy.trace_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.fault_policy = EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY;
    policy.log_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.block_due_timers = 1U;
    policy.block_actor_sleep_blockers = 1U;

    if ((app->irq_port != NULL) && (app->irq_port->get_stats != NULL)) {
        ev_irq_stats_t irq_stats = {0};
        rc = app->irq_port->get_stats(app->irq_port->ctx, &irq_stats);
        if (rc != EV_OK) {
            if (out_report != NULL) *out_report = report;
            return rc;
        }
        report.pending_irq_samples = irq_stats.pending_samples;
    } else if ((app->irq_port != NULL) && (app->irq_port->wait != NULL)) {
        rc = app->irq_port->wait(app->irq_port->ctx, 0U, &irq_pending);
        if (rc != EV_OK) {
            if (out_report != NULL) *out_report = report;
            return rc;
        }
        report.pending_irq_samples = irq_pending ? 1U : 0U;
    }

    rc = ev_demo_app_now_ms(app, &now_ms);
    if (rc != EV_OK) {
        if (out_report != NULL) *out_report = report;
        return rc;
    }
    rc = ev_runtime_is_quiescent_at(&app->graph, now_ms, &policy, &qreport);
    report.pending_actor_messages = qreport.pending_actor_messages;
    report.pending_oled_flush = ((qreport.sleep_blocker_actor_mask & (1UL << ACT_OLED)) != 0U) ? 1U : 0U;
    report.pending_ds18b20_conversion = ((qreport.sleep_blocker_actor_mask & (1UL << ACT_DS18B20)) != 0U) ? 1U : 0U;
    report.due_timer_count = qreport.due_timers;
    if ((rc != EV_OK) || (report.pending_irq_samples != 0U)) {
        report.reason = EV_POWER_SLEEP_REJECT_NOT_QUIESCENT;
        if (out_report != NULL) *out_report = report;
        return EV_ERR_STATE;
    }
    if (out_report != NULL) {
        *out_report = report;
    }
    return EV_OK;
}

static ev_result_t ev_demo_app_sleep_arm(void *ctx,
                                         uint64_t duration_us,
                                         ev_power_quiescence_report_t *out_report)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)duration_us;
    ++app->stats.sleep_arm_attempts;
    if (app->sleep_arming) {
        ++app->stats.sleep_arm_failures;
        if (out_report != NULL) {
            memset(out_report, 0, sizeof(*out_report));
            out_report->reason = EV_POWER_SLEEP_REJECT_ARMING_FAILED;
        }
        return EV_ERR_STATE;
    }

    app->sleep_arming = true;
    rc = ev_demo_app_sleep_quiescence_guard(app, duration_us, out_report);
    if (rc != EV_OK) {
        app->sleep_arming = false;
        ++app->stats.sleep_arm_failures;
        return rc;
    }

    ++app->stats.sleep_arm_successes;
    return EV_OK;
}

static ev_result_t ev_demo_app_sleep_disarm(void *ctx)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++app->stats.sleep_disarm_calls;
    app->sleep_arming = false;
    return EV_OK;
}

static void ev_demo_app_fill_watchdog_domain_snapshot(ev_domain_pump_t *domain_pump,
                                                        ev_watchdog_domain_snapshot_t *out_snapshot)
{
    const ev_domain_pump_stats_t *stats;

    if (out_snapshot == NULL) {
        return;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (domain_pump == NULL) {
        out_snapshot->domain = EV_DOMAIN_COUNT;
        out_snapshot->last_result = EV_ERR_STATE;
        return;
    }

    stats = ev_domain_pump_stats(domain_pump);
    out_snapshot->domain = domain_pump->domain;
    out_snapshot->bound = true;
    out_snapshot->pending_messages = ev_domain_pump_pending(domain_pump);
    if (stats != NULL) {
        out_snapshot->pump_calls = stats->pump_calls;
        out_snapshot->pump_empty_calls = stats->pump_empty_calls;
        out_snapshot->pump_budget_hits = stats->pump_budget_hits;
        out_snapshot->last_result = stats->last_result;
    }
}

static ev_result_t ev_demo_app_watchdog_liveness(void *ctx, ev_watchdog_liveness_snapshot_t *out_snapshot)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    const ev_system_pump_stats_t *system_stats;

    if ((app == NULL) || (out_snapshot == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    system_stats = ev_system_pump_stats(&app->graph.scheduler.system);
    if (system_stats == NULL) {
        return EV_ERR_STATE;
    }

    out_snapshot->system_turn_counter = system_stats->turns_processed;
    out_snapshot->system_messages_processed = system_stats->messages_processed;
    out_snapshot->system_pending_messages = ev_runtime_scheduler_pending(&app->graph.scheduler);
    out_snapshot->sleep_arming = app->sleep_arming;
    out_snapshot->permanent_stall = (system_stats->last_result != EV_OK) &&
                                    (system_stats->last_result != EV_ERR_EMPTY) &&
                                    (system_stats->last_result != EV_ERR_PARTIAL);
    out_snapshot->domain_count = 2U;
    ev_demo_app_fill_watchdog_domain_snapshot(&app->graph.scheduler.domains[EV_DOMAIN_FAST_LOOP], &out_snapshot->domains[0]);
    ev_demo_app_fill_watchdog_domain_snapshot(&app->graph.scheduler.domains[EV_DOMAIN_SLOW_IO], &out_snapshot->domains[1]);
    return EV_OK;
}

static bool ev_demo_app_budget_exhausted(const ev_poll_budget_t *budget)
{
    if (budget == NULL) {
        return true;
    }

    return (budget->pump_calls_used >= EV_APP_POLL_MAX_PUMP_TURNS) ||
           (budget->messages_used >= EV_APP_POLL_MAX_MESSAGES) ||
           (budget->turns_used >= EV_APP_POLL_MAX_PUMP_TURNS) ||
           (budget->irq_samples_used >= EV_APP_POLL_MAX_IRQ_SAMPLES) ||
           (budget->net_samples_used >= EV_APP_POLL_MAX_NET_SAMPLES);
}

static ev_result_t ev_demo_app_drain_budgeted(ev_demo_app_t *app,
                                              ev_demo_app_poll_diag_t *diag,
                                              ev_poll_budget_t *budget)
{
    ev_system_pump_report_t report = {0};
    ev_result_t rc;
    if ((app == NULL) || (budget == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    while ((ev_runtime_scheduler_pending(&app->graph.scheduler) > 0U) && !budget->exhausted) {
        if (ev_demo_app_budget_exhausted(budget)) {
            budget->exhausted = true;
            break;
        }
        rc = ev_runtime_scheduler_poll_once(&app->graph.scheduler, EV_DEMO_APP_TURN_BUDGET, &report);
        ++budget->pump_calls_used;
        budget->turns_used += report.turns_processed;
        budget->messages_used += report.messages_processed;
        if (diag != NULL) {
            ++diag->pump_calls;
            diag->turns += report.turns_processed;
            diag->messages += report.messages_processed;
        }
        if ((rc == EV_OK) || (rc == EV_ERR_PARTIAL)) {
            budget->exhausted = ev_demo_app_budget_exhausted(budget) || (rc == EV_ERR_PARTIAL);
            continue;
        }
        if (rc == EV_ERR_EMPTY) {
            return EV_OK;
        }
        ++app->stats.pump_errors;
        ev_demo_app_logf(app, EV_LOG_ERROR, "runtime scheduler rc=%d turns=%u messages=%u pending_after=%u", (int)rc, (unsigned)report.turns_processed, (unsigned)report.messages_processed, (unsigned)report.pending_after);
        return rc;
    }
    return EV_OK;
}

static ev_result_t ev_demo_app_collect_ingress(ev_demo_app_t *app,
                                               ev_poll_budget_t *budget,
                                               ev_demo_app_poll_diag_t *diag)
{
    ev_result_t rc;
    ev_irq_sample_t sample = {0};
    size_t reserved_used = 0U;

    if ((app == NULL) || (budget == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (app->sleep_arming || budget->exhausted) {
        return EV_OK;
    }

    if ((app->irq_port != NULL) && (app->irq_port->pop != NULL)) {
        while ((budget->irq_samples_used < EV_APP_POLL_MAX_IRQ_SAMPLES) &&
               !budget->exhausted &&
               (reserved_used < EV_APP_POLL_RESERVED_IRQ_SAMPLES)) {
            rc = app->irq_port->pop(app->irq_port->ctx, &sample);
            if (rc == EV_ERR_EMPTY) {
                break;
            }
            if (rc != EV_OK) {
                return rc;
            }

            ++budget->irq_samples_used;
            ++reserved_used;
            if (diag != NULL) {
                ++diag->irq_samples;
            }

            rc = ev_demo_app_publish_irq_sample(app, &sample);
            if (rc != EV_OK) {
                return rc;
            }

            budget->exhausted = ev_demo_app_budget_exhausted(budget);
        }

        if (budget->irq_samples_used >= EV_APP_POLL_MAX_IRQ_SAMPLES) {
            budget->exhausted = true;
        }
    }

    if (!app->sleep_arming && (app->net_port != NULL) && (app->net_port->poll_ingress != NULL) &&
        !budget->exhausted) {
        ev_net_ingress_event_t net_event;
        size_t net_reserved_used = 0U;

        while ((budget->net_samples_used < EV_APP_POLL_MAX_NET_SAMPLES) &&
               !budget->exhausted &&
               (net_reserved_used < EV_APP_POLL_RESERVED_NET_SAMPLES)) {
            memset(&net_event, 0, sizeof(net_event));
            rc = app->net_port->poll_ingress(app->net_port->ctx, &net_event);
            if (rc == EV_ERR_EMPTY) {
                break;
            }
            if (rc != EV_OK) {
                return rc;
            }

            ++budget->net_samples_used;
            ++net_reserved_used;
            ++app->stats.net_ingress_drained;
            if (diag != NULL) {
                ++diag->net_samples;
            }

            rc = ev_demo_app_publish_net_event(app, &net_event);
            if (rc != EV_OK) {
                return rc;
            }

            budget->exhausted = ev_demo_app_budget_exhausted(budget);
        }
    }

    if (budget->net_samples_used >= EV_APP_POLL_MAX_NET_SAMPLES) {
        budget->exhausted = true;
    }
    return EV_OK;
}


static void ev_demo_app_release_net_event_external_payload(const ev_net_ingress_event_t *event)
{
    if ((event != NULL) && (event->payload_storage == EV_NET_PAYLOAD_LEASE) &&
        (event->external_payload.release_fn != NULL) && (event->external_payload.data != NULL) &&
        (event->external_payload.size > 0U)) {
        event->external_payload.release_fn(
            event->external_payload.lifecycle_ctx,
            event->external_payload.data,
            event->external_payload.size);
    }
}

static ev_result_t ev_demo_app_publish_net_event(ev_demo_app_t *app, const ev_net_ingress_event_t *event)
{
    ev_msg_t msg = {0};
    ev_event_id_t event_id;
    ev_result_t rc;

    if ((app == NULL) || (event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (event->kind) {
    case EV_NET_EVENT_WIFI_UP:
        event_id = EV_NET_WIFI_UP;
        break;
    case EV_NET_EVENT_WIFI_DOWN:
        event_id = EV_NET_WIFI_DOWN;
        break;
    case EV_NET_EVENT_MQTT_UP:
        event_id = EV_NET_MQTT_UP;
        break;
    case EV_NET_EVENT_MQTT_DOWN:
        event_id = EV_NET_MQTT_DOWN;
        break;
    case EV_NET_EVENT_MQTT_MSG_RX:
        event_id = (event->payload_storage == EV_NET_PAYLOAD_LEASE) ? EV_NET_MQTT_MSG_RX_LEASE : EV_NET_MQTT_MSG_RX;
        break;
    default:
        ev_demo_app_release_net_event_external_payload(event);
        return EV_ERR_CONTRACT;
    }

    rc = ev_msg_init_publish(&msg, event_id, ACT_RUNTIME);
    if (rc != EV_OK) {
        ev_demo_app_release_net_event_external_payload(event);
        return rc;
    }
    if (event_id == EV_NET_MQTT_MSG_RX) {
        ev_net_mqtt_inline_payload_t inline_payload;

        if (event->payload_storage != EV_NET_PAYLOAD_INLINE) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return EV_ERR_CONTRACT;
        }
        if ((event->topic_len > EV_NET_MAX_TOPIC_BYTES) ||
            (event->payload_len > EV_NET_MAX_INLINE_PAYLOAD_BYTES)) {
            (void)ev_msg_dispose(&msg);
            return EV_ERR_CONTRACT;
        }
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.topic_len = event->topic_len;
        inline_payload.payload_len = event->payload_len;
        if (event->topic_len > 0U) {
            memcpy(inline_payload.topic, event->topic, event->topic_len);
        }
        if (event->payload_len > 0U) {
            memcpy(inline_payload.payload, event->payload, event->payload_len);
        }
        rc = ev_msg_set_inline_payload(&msg, &inline_payload, sizeof(inline_payload));
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            return rc;
        }
    } else if (event_id == EV_NET_MQTT_MSG_RX_LEASE) {
        if ((event->external_payload.data == NULL) ||
            (event->external_payload.size != sizeof(ev_net_mqtt_rx_payload_t)) ||
            (event->external_payload.retain_fn == NULL) ||
            (event->external_payload.release_fn == NULL)) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return EV_ERR_CONTRACT;
        }
        rc = ev_msg_set_external_payload(&msg,
                                         event->external_payload.data,
                                         event->external_payload.size,
                                         event->external_payload.retain_fn,
                                         event->external_payload.release_fn,
                                         event->external_payload.lifecycle_ctx);
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return rc;
        }
    }
    return ev_demo_app_publish_owned(app, &msg);
}


static ev_result_t ev_demo_app_timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_send(&app->graph, target_actor, msg);
}

static ev_result_t ev_demo_app_process_timers(ev_demo_app_t *app,
                                              ev_poll_budget_t *budget,
                                              uint32_t now_ms)
{
    size_t published;
    if ((app == NULL) || (budget == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (budget->exhausted || app->sleep_arming) {
        return EV_OK;
    }
    if (ev_runtime_scheduler_pending(&app->graph.scheduler) > 0U) {
        return EV_OK;
    }
    published = ev_timer_publish_due(&app->graph.timer_service, now_ms, ev_demo_app_timer_delivery, app, 1U);
    if (published > 0U) {
        budget->exhausted = ev_demo_app_budget_exhausted(budget);
    }
    return EV_OK;
}

ev_result_t ev_demo_runtime_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_demo_app_t *app = (ev_demo_app_t *)actor_context;
    if ((app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    switch (msg->event_id) {
    case EV_TICK_100MS:
        return ev_demo_app_publish_tick_100ms(app);
    case EV_TICK_1S:
        return ev_demo_app_publish_tick(app);
    default:
        return EV_OK;
    }
}

ev_result_t ev_demo_app_init(ev_demo_app_t *app, const ev_demo_app_config_t *cfg)
{
    ev_result_t rc;
    uint32_t now_ms;
    ev_i2c_port_t *active_i2c;
    ev_onewire_port_t *active_onewire;
    if ((app == NULL) || !ev_demo_app_config_is_valid(cfg)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(app, 0, sizeof(*app));
    app->clock_port = cfg->clock_port;
    app->log_port = cfg->log_port;
    app->irq_port = cfg->irq_port;
    app->system_port = cfg->system_port;
    app->wdt_port = cfg->wdt_port;
    app->net_port = cfg->net_port;
    app->app_tag = cfg->app_tag;
    app->board_name = cfg->board_name;
    app->tick_period_ms = (cfg->tick_period_ms == 0U) ? EV_DEMO_APP_DEFAULT_TICK_MS : cfg->tick_period_ms;
    app->board_profile = *((cfg->board_profile != NULL) ? cfg->board_profile : ev_demo_app_default_board_profile());
    app->app_actor.app = app;
    app->app_actor.direction_x = (int8_t)1;
    app->app_actor.direction_y = (int8_t)1;
    app->diag_actor.app = app;

    active_i2c = ((app->board_profile.hardware_present_mask &
                   (EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC | EV_SUPERVISOR_HW_OLED)) != 0U)
                     ? cfg->i2c_port
                     : NULL;
    active_onewire = ((app->board_profile.hardware_present_mask & EV_SUPERVISOR_HW_DS18B20) != 0U)
                       ? cfg->onewire_port
                       : NULL;

    rc = ev_demo_app_now_ms(app, &now_ms);
    if (rc != EV_OK) return rc;

    rc = ev_panel_actor_init(&app->panel_ctx, ev_demo_app_delivery, app);
    if (rc != EV_OK) return rc;
    rc = ev_supervisor_actor_init(&app->supervisor_ctx, ev_demo_app_delivery, app);
    if (rc != EV_OK) return rc;
    rc = ev_supervisor_actor_configure_hardware(&app->supervisor_ctx,
                                                app->board_profile.supervisor_required_mask,
                                                app->board_profile.supervisor_optional_mask);
    if (rc != EV_OK) return rc;
    rc = ev_power_actor_init(&app->power_ctx, app->system_port, app->log_port, app->app_tag);
    if (rc != EV_OK) return rc;
    rc = ev_command_actor_init(&app->command_ctx, ev_demo_app_delivery, app, app->board_profile.remote_command_token, app->board_profile.remote_command_capabilities);
    if (rc != EV_OK) return rc;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        rc = ev_watchdog_actor_init(&app->watchdog_ctx, app->wdt_port, app->board_profile.watchdog_timeout_ms, ev_demo_app_watchdog_liveness, app);
        if (rc != EV_OK) return rc;
    }
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) {
        if ((app->net_port == NULL) || (app->net_port->init == NULL) || (app->net_port->start == NULL)) {
            return EV_ERR_INVALID_ARG;
        }
        rc = app->net_port->init(app->net_port->ctx);
        if (rc != EV_OK) return rc;
        rc = app->net_port->start(app->net_port->ctx);
        if (rc != EV_OK) return rc;
        rc = ev_network_actor_init(&app->network_ctx, app->net_port);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_MCP23008)) {
        rc = ev_mcp23008_actor_init(&app->mcp23008_ctx, active_i2c, app->board_profile.i2c_port_num, app->board_profile.mcp23008_addr_7bit, ev_demo_app_delivery, app);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_RTC)) {
        rc = ev_rtc_actor_init(&app->rtc_ctx, active_i2c, app->irq_port, app->board_profile.i2c_port_num, app->board_profile.rtc_addr_7bit, app->board_profile.rtc_sqw_line_id, ev_demo_app_delivery, app);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_DS18B20)) {
        rc = ev_ds18b20_actor_init(&app->ds18b20_ctx, active_onewire, ev_demo_app_delivery, app);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_OLED)) {
        rc = ev_oled_actor_init(&app->oled_ctx, active_i2c, app->board_profile.i2c_port_num, app->board_profile.oled_addr_7bit, app->board_profile.oled_controller, ev_demo_app_delivery, app);
        if (rc != EV_OK) return rc;
    }
    rc = ev_demo_app_build_runtime_graph(app);
    if (rc != EV_OK) return rc;
    rc = ev_demo_app_schedule_standard_timers(app, now_ms);
    if (rc != EV_OK) return rc;

    rc = ev_power_actor_set_quiescence_guard(&app->power_ctx, ev_demo_app_sleep_quiescence_guard, app);
    if (rc != EV_OK) return rc;

    rc = ev_power_actor_set_sleep_arming(&app->power_ctx,
                                         ev_demo_app_sleep_arm,
                                         ev_demo_app_sleep_disarm,
                                         app);
    if (rc != EV_OK) return rc;

    rc = ev_lease_pool_init(&app->lease_pool, app->lease_slots, app->lease_storage, EV_DEMO_APP_LEASE_SLOTS, EV_DEMO_APP_LEASE_SLOT_BYTES);
    if (rc != EV_OK) return rc;

    ev_demo_app_logf(app, EV_LOG_INFO, "demo runtime ready board=%s tick_period_ms=%u", app->board_name, (unsigned)app->tick_period_ms);
    return EV_OK;
}

ev_result_t ev_demo_app_publish_boot(ev_demo_app_t *app)
{
    ev_msg_t msg = {0};
    ev_result_t rc;

    if (app == NULL) return EV_ERR_INVALID_ARG;
    if (app->boot_published) return EV_ERR_STATE;

    rc = ev_msg_init_publish(&msg, EV_BOOT_STARTED, ACT_BOOT);
    if (rc != EV_OK) return rc;

    rc = ev_demo_app_publish_owned(app, &msg);
    if (rc != EV_OK) return rc;

    rc = ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT);
    if (rc != EV_OK) return rc;

    rc = ev_demo_app_publish_owned(app, &msg);
    if (rc != EV_OK) return rc;

    app->boot_published = true;
    return EV_OK;
}

ev_result_t ev_demo_app_poll(ev_demo_app_t *app)
{
    ev_result_t rc = EV_OK;
    ev_demo_app_poll_diag_t diag;
    ev_poll_budget_t budget = {0};
    uint32_t now_ms = 0U;
    uint32_t start_ms = 0U;
    uint32_t end_ms = 0U;
    uint32_t elapsed_ms = 0U;
    size_t pending_before = 0U;
    size_t pending_after = 0U;
    bool have_timing = false;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!app->boot_published) {
        return EV_ERR_STATE;
    }

    ev_demo_app_poll_diag_reset(&diag);
    pending_before = ev_runtime_scheduler_pending(&app->graph.scheduler);
    if (ev_demo_app_now_ms(app, &start_ms) == EV_OK) {
        have_timing = true;
    }

    for (;;) {
        size_t before_irq_samples = budget.irq_samples_used;
        size_t before_net_samples = budget.net_samples_used;

        rc = ev_demo_app_collect_ingress(app, &budget, &diag);
        if (rc != EV_OK) {
            goto finalize;
        }

        rc = ev_demo_app_drain_budgeted(app, &diag, &budget);
        if (rc != EV_OK) {
            goto finalize;
        }

        if (budget.exhausted || ((budget.irq_samples_used == before_irq_samples) &&
                                  (budget.net_samples_used == before_net_samples))) {
            break;
        }
    }

    if (!budget.exhausted) {
        rc = ev_demo_app_now_ms(app, &now_ms);
        if (rc != EV_OK) {
            goto finalize;
        }

        for (;;) {
            const uint32_t before_timer_published = app->graph.timer_service.published;

            rc = ev_demo_app_process_timers(app, &budget, now_ms);
            if (rc != EV_OK) {
                goto finalize;
            }

            rc = ev_demo_app_drain_budgeted(app, &diag, &budget);
            if (rc != EV_OK) {
                goto finalize;
            }

            if (budget.exhausted ||
                (app->graph.timer_service.published == before_timer_published)) {
                break;
            }
        }
    }

finalize:
    pending_after = ev_runtime_scheduler_pending(&app->graph.scheduler);
    if (have_timing && (ev_demo_app_now_ms(app, &end_ms) == EV_OK)) {
        elapsed_ms = end_ms - start_ms;
    }
    ev_demo_app_record_poll_diag(app, &diag, pending_before, pending_after, elapsed_ms);
    ev_demo_app_record_irq_stats(app);
    ev_demo_app_record_net_stats(app);
    if ((rc == EV_OK) && budget.exhausted) {
        bool irq_work_pending = false;
        bool net_work_pending = false;
        uint32_t current_now_ms = end_ms;
        bool timer_due = false;

        if ((app->irq_port != NULL) && (app->irq_port->wait != NULL)) {
            (void)app->irq_port->wait(app->irq_port->ctx, 0U, &irq_work_pending);
        }
        if ((app->net_port != NULL) && (app->net_port->get_stats != NULL)) {
            ev_net_stats_t net_stats;
            if (app->net_port->get_stats(app->net_port->ctx, &net_stats) == EV_OK) {
                net_work_pending = (net_stats.pending_events > 0U);
            }
        }
        if (!have_timing || (ev_demo_app_now_ms(app, &current_now_ms) != EV_OK)) {
            current_now_ms = end_ms;
        }
        {
            ev_quiescence_report_t q = {0};
            ev_quiescence_policy_t policy = {0};
            policy.block_due_timers = 1U;
            if (ev_runtime_is_quiescent_at(&app->graph, current_now_ms, &policy, &q) != EV_OK) {
                timer_due = (q.due_timers > 0U);
            }
        }
        if ((pending_after > 0U) || irq_work_pending || net_work_pending || timer_due) {
            rc = EV_ERR_PARTIAL;
        }
    }
    return rc;
}

size_t ev_demo_app_pending(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_runtime_scheduler_pending(&app->graph.scheduler) : 0U;
}


ev_result_t ev_demo_app_next_deadline_ms(const ev_demo_app_t *app, uint32_t *out_deadline_ms)
{
    if ((app == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_next_deadline_ms(&app->graph, out_deadline_ms);
}

ev_result_t ev_demo_app_post_event(ev_demo_app_t *app,
                                   ev_event_id_t event_id,
                                   ev_actor_id_t source_actor,
                                   const void *payload,
                                   size_t payload_size)
{
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_post_event(&app->graph, event_id, source_actor, payload, payload_size);
}

const ev_demo_app_stats_t *ev_demo_app_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? &app->stats : NULL;
}

const ev_system_pump_stats_t *ev_demo_app_system_pump_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_system_pump_stats(&app->graph.scheduler.system) : NULL;
}

const ev_watchdog_actor_stats_t *ev_demo_app_watchdog_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_watchdog_actor_stats(&app->watchdog_ctx) : NULL;
}

const ev_network_actor_stats_t *ev_demo_app_network_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_network_actor_stats(&app->network_ctx) : NULL;
}

const ev_command_actor_stats_t *ev_demo_app_command_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_command_actor_stats(&app->command_ctx) : NULL;
}
