#include "ev/ds18b20_actor.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_DS18B20_CMD_SKIP_ROM 0xCCU
#define EV_DS18B20_CMD_CONVERT_T 0x44U
#define EV_DS18B20_CMD_READ_SCRATCHPAD 0xBEU
#define EV_DS18B20_SCRATCHPAD_BYTES 9U
#define EV_DS18B20_CFG_RESOLUTION_MASK 0x60U
#define EV_DS18B20_CFG_9BIT 0x00U
#define EV_DS18B20_CFG_10BIT 0x20U
#define EV_DS18B20_CFG_11BIT 0x40U
#define EV_DS18B20_CFG_12BIT 0x60U
#define EV_DS18B20_CRC_POLY 0x8CU

static uint8_t ev_ds18b20_actor_crc8(const uint8_t *data, size_t data_len)
{
    uint8_t crc = 0U;
    size_t i;

    if (data == NULL) {
        return 0U;
    }

    for (i = 0U; i < data_len; ++i) {
        uint8_t current = data[i];
        uint8_t bit;

        for (bit = 0U; bit < 8U; ++bit) {
            const uint8_t mix = (uint8_t)((crc ^ current) & 0x01U);
            crc = (uint8_t)(crc >> 1U);
            if (mix != 0U) {
                crc ^= EV_DS18B20_CRC_POLY;
            }
            current = (uint8_t)(current >> 1U);
        }
    }

    return crc;
}

static ev_onewire_status_t ev_ds18b20_actor_start_conversion(ev_ds18b20_actor_ctx_t *ctx)
{
    ev_onewire_status_t status;

    if ((ctx == NULL) || (ctx->onewire_port == NULL) || (ctx->onewire_port->reset == NULL) ||
        (ctx->onewire_port->write_byte == NULL)) {
        return EV_ONEWIRE_ERR_BUS;
    }

    status = ctx->onewire_port->reset(ctx->onewire_port->ctx);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    status = ctx->onewire_port->write_byte(ctx->onewire_port->ctx, EV_DS18B20_CMD_SKIP_ROM);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    status = ctx->onewire_port->write_byte(ctx->onewire_port->ctx, EV_DS18B20_CMD_CONVERT_T);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }

    return EV_ONEWIRE_OK;
}

static ev_result_t ev_ds18b20_actor_read_scratchpad(ev_ds18b20_actor_ctx_t *ctx,
                                                    uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES])
{
    ev_onewire_status_t status;
    size_t i;

    if ((ctx == NULL) || (scratchpad == NULL) || (ctx->onewire_port == NULL) || (ctx->onewire_port->reset == NULL) ||
        (ctx->onewire_port->write_byte == NULL) || (ctx->onewire_port->read_byte == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    status = ctx->onewire_port->reset(ctx->onewire_port->ctx);
    if (status == EV_ONEWIRE_ERR_NO_DEVICE) {
        return EV_ERR_NOT_FOUND;
    }
    if (status != EV_ONEWIRE_OK) {
        return EV_ERR_STATE;
    }
    if (ctx->onewire_port->write_byte(ctx->onewire_port->ctx, EV_DS18B20_CMD_SKIP_ROM) != EV_ONEWIRE_OK) {
        return EV_ERR_STATE;
    }
    if (ctx->onewire_port->write_byte(ctx->onewire_port->ctx, EV_DS18B20_CMD_READ_SCRATCHPAD) != EV_ONEWIRE_OK) {
        return EV_ERR_STATE;
    }

    for (i = 0U; i < EV_DS18B20_SCRATCHPAD_BYTES; ++i) {
        if (ctx->onewire_port->read_byte(ctx->onewire_port->ctx, &scratchpad[i]) != EV_ONEWIRE_OK) {
            return EV_ERR_STATE;
        }
    }

    if (ev_ds18b20_actor_crc8(scratchpad, EV_DS18B20_SCRATCHPAD_BYTES - 1U) != scratchpad[EV_DS18B20_SCRATCHPAD_BYTES - 1U]) {
        return EV_ERR_CONTRACT;
    }

    return EV_OK;
}

static int16_t ev_ds18b20_actor_decode_centi_celsius(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES])
{
    int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8U) | (uint16_t)scratchpad[0]);
    int32_t scaled;

    switch (scratchpad[4] & EV_DS18B20_CFG_RESOLUTION_MASK) {
    case EV_DS18B20_CFG_9BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0007));
        break;
    case EV_DS18B20_CFG_10BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0003));
        break;
    case EV_DS18B20_CFG_11BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0001));
        break;
    case EV_DS18B20_CFG_12BIT:
    default:
        break;
    }

    scaled = (int32_t)raw * 25;
    if (scaled >= 0) {
        scaled = (scaled + 2) / 4;
    } else {
        scaled = (scaled - 2) / 4;
    }

    return (int16_t)scaled;
}

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

static void ev_ds18b20_actor_record_start_result(ev_ds18b20_actor_ctx_t *ctx, ev_onewire_status_t status)
{
    if (ctx == NULL) {
        return;
    }

    if (status == EV_ONEWIRE_OK) {
        ctx->sensor_present = true;
        ctx->conversion_pending = true;
        ++ctx->conversions_started;
        return;
    }

    ctx->sensor_present = false;
    ctx->conversion_pending = false;
    if (status == EV_ONEWIRE_ERR_NO_DEVICE) {
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

    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_actor_start_conversion(ctx));
    return EV_OK;
}

static ev_result_t ev_ds18b20_actor_handle_tick(ev_ds18b20_actor_ctx_t *ctx)
{
    uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES] = {0};
    ev_result_t publish_rc = EV_OK;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (ctx->conversion_pending) {
        ev_result_t rc = ev_ds18b20_actor_read_scratchpad(ctx, scratchpad);

        if (rc == EV_OK) {
            const int16_t centi_celsius = ev_ds18b20_actor_decode_centi_celsius(scratchpad);
            const bool was_valid = ctx->temp_valid;

            ctx->sensor_present = true;
            ctx->last_read_ok = true;
            ctx->temp_valid = true;
            ctx->last_centi_celsius = centi_celsius;
            ++ctx->scratchpad_reads_ok;
            if (!was_valid) {
                publish_rc = ev_ds18b20_actor_publish_ready(ctx);
                if (publish_rc != EV_OK) {
                    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_actor_start_conversion(ctx));
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

    ev_ds18b20_actor_record_start_result(ctx, ev_ds18b20_actor_start_conversion(ctx));
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
