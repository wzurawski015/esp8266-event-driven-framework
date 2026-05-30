#include "ev/supervisor_actor.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

static ev_result_t ev_supervisor_actor_publish_system_ready(ev_supervisor_actor_ctx_t *ctx)
{
    ev_system_ready_payload_t payload;
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload.active_hardware_mask = ctx->active_hardware_mask;
    rc = ev_msg_init_publish(&msg, EV_SYSTEM_READY, ACT_SUPERVISOR);
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

static void ev_supervisor_actor_mark_ready(ev_supervisor_actor_ctx_t *ctx, uint32_t hw_mask)
{
    if (ctx == NULL) {
        return;
    }

    ctx->observed_hardware_mask |= hw_mask;
    ctx->active_hardware_mask |= hw_mask;
}

static void ev_supervisor_actor_mark_remaining_offline(ev_supervisor_actor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->observed_hardware_mask |= ctx->known_hardware_mask;
}

static ev_result_t ev_supervisor_actor_try_publish_if_settled(ev_supervisor_actor_ctx_t *ctx)
{
    uint32_t settled_mask;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ctx->boot_observed || ctx->system_ready_published) {
        return EV_OK;
    }

    settled_mask = ctx->observed_hardware_mask & ctx->required_hardware_mask;
    if ((settled_mask == ctx->required_hardware_mask) ||
        (ctx->ticks_waited >= EV_SUPERVISOR_BOOT_SETTLE_TICKS)) {
        if (ctx->ticks_waited >= EV_SUPERVISOR_BOOT_SETTLE_TICKS) {
            ev_supervisor_actor_mark_remaining_offline(ctx);
        }
        ctx->system_ready_published = true;
        return ev_supervisor_actor_publish_system_ready(ctx);
    }

    return EV_OK;
}

static ev_result_t ev_supervisor_actor_handle_ready(ev_supervisor_actor_ctx_t *ctx, uint32_t hw_mask)
{
    bool became_active;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    became_active = ((ctx->active_hardware_mask & hw_mask) == 0U);
    ev_supervisor_actor_mark_ready(ctx, hw_mask);

    if (!ctx->system_ready_published) {
        return ev_supervisor_actor_try_publish_if_settled(ctx);
    }

    if (became_active) {
        return ev_supervisor_actor_publish_system_ready(ctx);
    }

    return EV_OK;
}

ev_result_t ev_supervisor_actor_init(ev_supervisor_actor_ctx_t *ctx,
                                     ev_delivery_fn_t deliver,
                                     void *deliver_context)
{
    if ((ctx == NULL) || (deliver == NULL) || (deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    ctx->required_hardware_mask = EV_SUPERVISOR_REQUIRED_MASK;
    ctx->optional_hardware_mask = EV_SUPERVISOR_OPTIONAL_MASK;
    ctx->known_hardware_mask = EV_SUPERVISOR_KNOWN_MASK;
    return EV_OK;
}

ev_result_t ev_supervisor_actor_configure_hardware(ev_supervisor_actor_ctx_t *ctx,
                                                   uint32_t required_hardware_mask,
                                                   uint32_t optional_hardware_mask)
{
    const uint32_t known_mask = required_hardware_mask | optional_hardware_mask;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if ((known_mask & (uint32_t)(~EV_SUPERVISOR_KNOWN_MASK)) != 0U) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if ((required_hardware_mask & optional_hardware_mask) != 0U) {
        return EV_ERR_CONTRACT;
    }

    ctx->required_hardware_mask = required_hardware_mask;
    ctx->optional_hardware_mask = optional_hardware_mask;
    ctx->known_hardware_mask = known_mask;
    return EV_OK;
}

ev_result_t ev_supervisor_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_supervisor_actor_ctx_t *ctx = (ev_supervisor_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        ctx->boot_observed = true;
        ctx->system_ready_published = false;
        ctx->ticks_waited = 0U;
        ctx->active_hardware_mask = 0U;
        ctx->observed_hardware_mask = 0U;
        return EV_OK;

    case EV_MCP23008_READY:
        return ev_supervisor_actor_handle_ready(ctx, EV_SUPERVISOR_HW_MCP23008);

    case EV_RTC_READY:
        return ev_supervisor_actor_handle_ready(ctx, EV_SUPERVISOR_HW_RTC);

    case EV_OLED_READY:
        return ev_supervisor_actor_handle_ready(ctx, EV_SUPERVISOR_HW_OLED);

    case EV_DS18B20_READY:
        return ev_supervisor_actor_handle_ready(ctx, EV_SUPERVISOR_HW_DS18B20);

    case EV_TICK_1S:
        if (ctx->boot_observed && !ctx->system_ready_published) {
            ++ctx->ticks_waited;
            return ev_supervisor_actor_try_publish_if_settled(ctx);
        }
        return EV_OK;

    default:
        return EV_ERR_CONTRACT;
    }
}
