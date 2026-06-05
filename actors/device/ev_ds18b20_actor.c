#include "ev/ds18b20_actor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/ds18b20_driver.h"
#include "ev/msg.h"
#include "ev/publish.h"

static ev_result_t ev_ds18b20_actor_publish_ready(ev_ds18b20_actor_ctx_t *ctx)
{
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_DS18B20_READY, ACT_DS18B20);
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_result_t ev_ds18b20_actor_publish_temperature(ev_ds18b20_actor_ctx_t *ctx, int16_t centi_celsius)
{

    ev_temp_payload_t payload;
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    payload.centi_celsius = centi_celsius;
    rc = ev_msg_init_publish(&msg, EV_TEMP_UPDATED, ACT_DS18B20);
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

static void ev_ds18b20_actor_record_start_result(ev_ds18b20_actor_ctx_t *ctx, ev_result_t status)
{
    if (ctx == NULL) {
        return;
    }

    if (status == EV_OK) {
        ctx->sensor_present = true;
        ctx->conversion_pending = true;
        ++ctx->conversions_started;
        return;
    }

    ctx->sensor_present = false;
    ctx->conversion_pending = false;
    if (status == EV_ERR_NOT_FOUND) {
        ++ctx->no_device_failures;
    } else {
        ++ctx->io_failures;
    }
}

static ev_result_t ev_ds18b20_actor_handle_boot(ev_ds18b20_actor_ctx_t *ctx)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_start_conversion_skip_rom(ctx->onewire_port));
    return EV_OK;
}

static ev_result_t ev_ds18b20_actor_handle_tick(ev_ds18b20_actor_ctx_t *ctx)
{
    uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES] = {0};
    ev_result_t publish_rc = EV_OK;
    int16_t centi_celsius = 0;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (ctx->conversion_pending) {
        ev_result_t rc = ev_ds18b20_read_scratchpad_skip_rom(ctx->onewire_port, scratchpad);

        if (rc == EV_OK) {
            rc = ev_ds18b20_decode_centi_celsius(scratchpad, &centi_celsius);
        }
        if (rc == EV_OK) {
            const bool was_valid = ctx->temp_valid;

            ctx->sensor_present = true;
            ctx->last_read_ok = true;
            ctx->temp_valid = true;
            ctx->last_centi_celsius = centi_celsius;
            ++ctx->scratchpad_reads_ok;
            if (!was_valid) {
                publish_rc = ev_ds18b20_actor_publish_ready(ctx);
                if (publish_rc != EV_OK) {
                    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_start_conversion_skip_rom(ctx->onewire_port));
                    return publish_rc;
                }
            }
            publish_rc = ev_ds18b20_actor_publish_temperature(ctx, centi_celsius);
        } else {
            ctx->last_read_ok = false;
            if (rc == EV_ERR_CONTRACT) {
                ++ctx->crc_failures;
            } else if (rc == EV_ERR_NOT_FOUND) {
                ctx->sensor_present = false;
                ++ctx->no_device_failures;
            } else if (rc == EV_ERR_STATE) {
                ++ctx->io_failures;
            }
        }
    }

    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_start_conversion_skip_rom(ctx->onewire_port));
    return publish_rc;
}

ev_result_t ev_ds18b20_actor_init(ev_ds18b20_actor_ctx_t *ctx,
                                  ev_onewire_port_t *onewire_port,
                                  ev_delivery_fn_t deliver,
                                  void *deliver_context)
{
    if ((ctx == NULL) || (onewire_port == NULL) || (onewire_port->reset == NULL) ||
        (onewire_port->write_byte == NULL) || (onewire_port->read_byte == NULL) || (deliver == NULL) ||
        (deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->onewire_port = onewire_port;
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    return EV_OK;
}

ev_result_t ev_ds18b20_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_ds18b20_actor_ctx_t *ctx = (ev_ds18b20_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        return ev_ds18b20_actor_handle_boot(ctx);

    case EV_TICK_1S:
        return ev_ds18b20_actor_handle_tick(ctx);

    default:
        return EV_ERR_CONTRACT;
    }
}
