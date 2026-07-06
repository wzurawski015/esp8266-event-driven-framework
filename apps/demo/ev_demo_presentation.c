#include "ev/demo_presentation.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/compiler.h"
#include "ev/demo_internal.h"

#define EV_DEMO_APP_OLED_TITLE_TEXT "ATB THERMO"
#define EV_DEMO_APP_OLED_TITLE_PAGES 3U
#define EV_DEMO_APP_OLED_TITLE_PAGE_OFFSET 0U
#define EV_DEMO_APP_OLED_TIME_PAGE_OFFSET 1U
#define EV_DEMO_APP_OLED_TEMP_PAGE_OFFSET 2U
#define EV_DEMO_APP_OLED_TEXT_CELL_ADVANCE 6U
#define EV_DEMO_APP_OLED_BLOCK_WIDTH_PX ((uint8_t)((sizeof(EV_DEMO_APP_OLED_TITLE_TEXT) - 1U) * EV_DEMO_APP_OLED_TEXT_CELL_ADVANCE))
#define EV_DEMO_APP_OLED_MAX_COLUMN_OFFSET ((uint8_t)(EV_OLED_WIDTH - EV_DEMO_APP_OLED_BLOCK_WIDTH_PX))
#define EV_DEMO_APP_OLED_MAX_PAGE_OFFSET ((uint8_t)(EV_OLED_PAGE_COUNT - EV_DEMO_APP_OLED_TITLE_PAGES))

EV_STATIC_ASSERT(EV_DEMO_APP_OLED_BLOCK_WIDTH_PX <= EV_OLED_WIDTH, "OLED block width must fit the panel");
EV_STATIC_ASSERT(EV_DEMO_APP_OLED_TITLE_PAGES <= EV_OLED_PAGE_COUNT, "OLED block height must fit the panel");

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

static void ev_demo_app_format_light_text(const ev_demo_app_actor_state_t *state, char *out_text, size_t out_text_size)
{
    uint32_t milli_lux;

    if ((state == NULL) || (out_text == NULL) || (out_text_size == 0U)) {
        return;
    }

    if (!state->light_valid) {
        (void)snprintf(out_text, out_text_size, "---- lx");
        return;
    }

    milli_lux = state->last_light.milli_lux;
    (void)snprintf(out_text,
                   out_text_size,
                   "%lu.%03lu lx",
                   (unsigned long)(milli_lux / 1000U),
                   (unsigned long)(milli_lux % 1000U));
}


ev_result_t ev_demo_app_render_oled_frame(ev_demo_app_actor_state_t *state)
{
    ev_demo_app_t *app;
    char time_text[EV_OLED_TEXT_MAX_CHARS] = {0};
    char temp_text[EV_OLED_TEXT_MAX_CHARS] = {0};
    char light_text[EV_OLED_TEXT_MAX_CHARS] = {0};
    ev_result_t rc;

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    if (!ev_demo_app_hardware_active(state, EV_SUPERVISOR_HW_OLED)) {
        return EV_OK;
    }
    ev_demo_app_format_time_text(state, time_text, sizeof(time_text));
    ev_demo_app_format_temp_text(state, temp_text, sizeof(temp_text));
    ev_demo_app_format_light_text(state, light_text, sizeof(light_text));

    memset(&state->oled_scene, 0, sizeof(state->oled_scene));
    state->oled_scene.page_offset = state->current_page_offset;
    state->oled_scene.column_offset = state->current_column_offset;
    state->oled_scene.flags = EV_OLED_SCENE_FLAG_VISIBLE;
    (void)snprintf(state->oled_scene.lines[0], sizeof(state->oled_scene.lines[0]), "%s", EV_DEMO_APP_OLED_TITLE_TEXT);
    (void)snprintf(state->oled_scene.lines[1],
                   sizeof(state->oled_scene.lines[1]),
                   "T %.*s",
                   (int)(EV_OLED_TEXT_MAX_CHARS - 3U),
                   temp_text);
    if (state->light_valid) {
        (void)snprintf(state->oled_scene.lines[2],
                       sizeof(state->oled_scene.lines[2]),
                       "L %.*s",
                       (int)(EV_OLED_TEXT_MAX_CHARS - 3U),
                       light_text);
    } else {
        (void)snprintf(state->oled_scene.lines[2], sizeof(state->oled_scene.lines[2]), "%s", time_text);
    }

    state->last_page_offset = state->current_page_offset;
    state->last_column_offset = state->current_column_offset;
    state->oled_frame_visible = true;
    rc = ev_demo_app_publish_oled_scene_commit(app, &state->oled_scene);
    if (rc == EV_OK) {
        ev_demo_app_logf(app,
                         EV_LOG_INFO,
                         "EV_OLED_FRAME line0='%s' line1='%s' line2='%s'",
                         state->oled_scene.lines[0],
                         state->oled_scene.lines[1],
                         state->oled_scene.lines[2]);
    }
    return rc;
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

ev_result_t ev_demo_app_handle_tick_for_oled(ev_demo_app_actor_state_t *state)
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
