#include "ev/panel_actor.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/mcp23008_actor.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_PANEL_DEBOUNCE_TICKS 1U
#define EV_PANEL_LONG_PRESS_TICKS 10U

static ev_result_t ev_panel_actor_publish_button_event(ev_panel_actor_ctx_t *ctx,
                                                       uint8_t button_id,
                                                       uint8_t action)
{
    ev_button_event_payload_t payload;
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL) || (button_id >= EV_PANEL_BUTTON_COUNT)) {
        return EV_ERR_INVALID_ARG;
    }

    payload.button_id = button_id;
    payload.action = action;

    rc = ev_msg_init_publish(&msg, EV_BUTTON_EVENT, ACT_PANEL);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, &payload, sizeof(payload));
    }
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_result_t ev_panel_actor_handle_inputs_changed(ev_panel_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_mcp23008_input_payload_t *payload;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload = (const ev_mcp23008_input_payload_t *)ev_msg_payload_data(msg);
    if ((payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*payload))) {
        return EV_ERR_CONTRACT;
    }

    ctx->raw_mask = (uint8_t)(payload->pressed_mask & EV_MCP23008_BUTTON_MASK);
    ctx->raw_valid = true;
    return EV_OK;
}

static ev_result_t ev_panel_actor_handle_tick(ev_panel_actor_ctx_t *ctx)
{
    uint8_t button_id;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ctx->raw_valid) {
        return EV_OK;
    }

    for (button_id = 0U; button_id < EV_PANEL_BUTTON_COUNT; ++button_id) {
        const uint8_t bit = (uint8_t)(1U << button_id);
        const bool raw_pressed = (ctx->raw_mask & bit) != 0U;
        const bool stable_pressed = (ctx->stable_mask & bit) != 0U;

        if (raw_pressed != stable_pressed) {
            if (ctx->debounce_ticks[button_id] < UCHAR_MAX) {
                ++ctx->debounce_ticks[button_id];
            }

            if (ctx->debounce_ticks[button_id] >= EV_PANEL_DEBOUNCE_TICKS) {
                ev_result_t rc;

                ctx->debounce_ticks[button_id] = 0U;
                if (raw_pressed) {
                    ctx->stable_mask = (uint8_t)(ctx->stable_mask | bit);
                    ctx->hold_ticks[button_id] = 0U;
                    ctx->long_reported_mask = (uint8_t)(ctx->long_reported_mask & (uint8_t)(~bit));
                    rc = ev_panel_actor_publish_button_event(ctx, button_id, EV_BUTTON_ACTION_PRESS);
                } else {
                    ctx->stable_mask = (uint8_t)(ctx->stable_mask & (uint8_t)(~bit));
                    rc = ev_panel_actor_publish_button_event(ctx, button_id, EV_BUTTON_ACTION_RELEASE);
                    if (rc != EV_OK) {
                        return rc;
                    }
                    if ((ctx->long_reported_mask & bit) == 0U) {
                        rc = ev_panel_actor_publish_button_event(ctx, button_id, EV_BUTTON_ACTION_SHORT);
                    }
                    ctx->hold_ticks[button_id] = 0U;
                    ctx->long_reported_mask = (uint8_t)(ctx->long_reported_mask & (uint8_t)(~bit));
                }

                if (rc != EV_OK) {
                    return rc;
                }
            }
        } else {
            ctx->debounce_ticks[button_id] = 0U;
            if (stable_pressed) {
                if (ctx->hold_ticks[button_id] < UINT16_MAX) {
                    ++ctx->hold_ticks[button_id];
                }
                if ((ctx->hold_ticks[button_id] >= EV_PANEL_LONG_PRESS_TICKS) &&
                    ((ctx->long_reported_mask & bit) == 0U)) {
                    ev_result_t rc = ev_panel_actor_publish_button_event(ctx, button_id, EV_BUTTON_ACTION_LONG);

                    if (rc != EV_OK) {
                        return rc;
                    }
                    ctx->long_reported_mask = (uint8_t)(ctx->long_reported_mask | bit);
                }
            }
        }
    }

    return EV_OK;
}

ev_result_t ev_panel_actor_init(ev_panel_actor_ctx_t *ctx,
                                ev_delivery_fn_t deliver,
                                void *deliver_context)
{
    if ((ctx == NULL) || (deliver == NULL) || (deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    return EV_OK;
}

ev_result_t ev_panel_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_panel_actor_ctx_t *ctx = (ev_panel_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_MCP23008_INPUT_CHANGED:
        return ev_panel_actor_handle_inputs_changed(ctx, msg);

    case EV_TICK_100MS:
        return ev_panel_actor_handle_tick(ctx);

    default:
        return EV_ERR_CONTRACT;
    }
}
