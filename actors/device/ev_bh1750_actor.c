#include "ev/bh1750_actor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_BH1750_TICK_100MS_DELTA_MS 100U

static bool ev_bh1750_actor_deadline_due(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms) >= 0) ? true : false;
}

static ev_result_t ev_bh1750_actor_publish_ready(ev_bh1750_actor_ctx_t *ctx)
{
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_BH1750_READY, ACT_BH1750);
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }
    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }
    return rc;
}

static ev_result_t ev_bh1750_actor_publish_light(ev_bh1750_actor_ctx_t *ctx,
                                                 uint16_t raw,
                                                 uint32_t milli_lux)
{
    ev_light_payload_t payload;
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload.raw = raw;
    payload.milli_lux = milli_lux;
    rc = ev_msg_init_publish(&msg, EV_LIGHT_UPDATED, ACT_BH1750);
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

static void ev_bh1750_actor_record_start(ev_bh1750_actor_ctx_t *ctx, ev_result_t status)
{
    if (ctx == NULL) {
        return;
    }
    if (status == EV_OK) {
        ctx->sensor_present = true;
        ctx->measurement_pending = true;
        ctx->measurement_deadline_ms = ctx->actor_now_ms + (uint32_t)ctx->measurement_wait_ms;
        ++ctx->measurements_started;
    } else {
        ctx->measurement_pending = false;
        if (status == EV_ERR_NOT_FOUND) {
            ctx->sensor_present = false;
            ++ctx->no_device_failures;
        } else {
            ++ctx->io_failures;
        }
    }
}

static ev_result_t ev_bh1750_actor_start_measurement(ev_bh1750_actor_ctx_t *ctx)
{
    ev_result_t rc;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_bh1750_start_measurement(ctx->i2c_port, ctx->port_num, ctx->addr7, ctx->mode);
    ev_bh1750_actor_record_start(ctx, rc);
    return EV_OK;
}

static ev_result_t ev_bh1750_actor_boot(ev_bh1750_actor_ctx_t *ctx)
{
    ev_result_t rc;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_bh1750_power_on(ctx->i2c_port, ctx->port_num, ctx->addr7);
    if (rc != EV_OK) {
        ev_bh1750_actor_record_start(ctx, rc);
        return EV_OK;
    }
    return ev_bh1750_actor_start_measurement(ctx);
}

static ev_result_t ev_bh1750_actor_try_read(ev_bh1750_actor_ctx_t *ctx)
{
    uint16_t raw = 0U;
    uint32_t milli_lux = 0U;
    ev_result_t rc;
    ev_result_t publish_rc = EV_OK;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ctx->measurement_pending) {
        return EV_OK;
    }
    if (!ev_bh1750_actor_deadline_due(ctx->actor_now_ms, ctx->measurement_deadline_ms)) {
        ++ctx->deadline_skips;
        return EV_OK;
    }

    ctx->measurement_pending = false;
    rc = ev_bh1750_read_measurement(ctx->i2c_port, ctx->port_num, ctx->addr7, &raw, &milli_lux);
    if (rc == EV_OK) {
        ctx->sensor_present = true;
        ctx->last_read_ok = true;
        ctx->last_raw = raw;
        ctx->last_milli_lux = milli_lux;
        ctx->light_valid = true;
        ++ctx->measurements_ok;
        if (!ctx->ready_published) {
            publish_rc = ev_bh1750_actor_publish_ready(ctx);
            if (publish_rc != EV_OK) {
                return publish_rc;
            }
            ctx->ready_published = true;
        }
        publish_rc = ev_bh1750_actor_publish_light(ctx, raw, milli_lux);
    } else {
        ctx->last_read_ok = false;
        if (rc == EV_ERR_NOT_FOUND) {
            ctx->sensor_present = false;
            ++ctx->no_device_failures;
        } else {
            ++ctx->io_failures;
        }
    }

    if (publish_rc == EV_OK) {
        return ev_bh1750_actor_start_measurement(ctx);
    }
    return publish_rc;
}

static ev_result_t ev_bh1750_actor_tick(ev_bh1750_actor_ctx_t *ctx, uint32_t elapsed_ms)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ctx->actor_now_ms += elapsed_ms;
    if (!ctx->measurement_pending) {
        return ev_bh1750_actor_start_measurement(ctx);
    }
    return ev_bh1750_actor_try_read(ctx);
}

ev_result_t ev_bh1750_actor_init(ev_bh1750_actor_ctx_t *ctx,
                                 ev_i2c_port_t *i2c_port,
                                 ev_i2c_port_num_t port_num,
                                 uint8_t addr7,
                                 ev_delivery_fn_t deliver,
                                 void *deliver_context)
{
    if ((ctx == NULL) || (i2c_port == NULL) || (i2c_port->write_stream == NULL) ||
        (i2c_port->read_stream == NULL) || (deliver == NULL) || (deliver_context == NULL) ||
        (ev_bh1750_validate_addr7(addr7) != EV_OK)) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->i2c_port = i2c_port;
    ctx->port_num = port_num;
    ctx->addr7 = addr7;
    ctx->mode = EV_BH1750_DEFAULT_MODE;
    ctx->measurement_wait_ms = ev_bh1750_measurement_wait_ms(ctx->mode);
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    return EV_OK;
}

ev_result_t ev_bh1750_actor_configure_mode(ev_bh1750_actor_ctx_t *ctx, ev_bh1750_mode_t mode)
{
    ev_result_t rc;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (ctx->measurement_pending) {
        return EV_ERR_STATE;
    }
    rc = ev_bh1750_validate_mode(mode);
    if (rc != EV_OK) {
        return rc;
    }
    ctx->mode = mode;
    ctx->measurement_wait_ms = ev_bh1750_measurement_wait_ms(mode);
    return EV_OK;
}

ev_result_t ev_bh1750_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_bh1750_actor_ctx_t *ctx = (ev_bh1750_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        return ev_bh1750_actor_boot(ctx);
    case EV_TICK_100MS:
        return ev_bh1750_actor_tick(ctx, EV_BH1750_TICK_100MS_DELTA_MS);
    case EV_TICK_1S:
        ++ctx->noncanonical_ticks_ignored;
        return EV_OK;
    default:
        return EV_ERR_CONTRACT;
    }
}
