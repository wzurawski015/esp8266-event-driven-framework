#include "ev/rtc_actor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"

#define EV_RTC_REG_SECONDS 0x00U
#define EV_RTC_REG_CONTROL 0x0EU
#define EV_RTC_TIME_BYTES 7U
#define EV_RTC_CONTROL_1HZ_SQW 0x00U
#define EV_RTC_SECONDS_MASK 0x7FU
#define EV_RTC_MINUTES_MASK 0x7FU
#define EV_RTC_HOURS_12H_MODE_MASK 0x40U
#define EV_RTC_HOURS_PM_MASK 0x20U
#define EV_RTC_HOURS_12H_VALUE_MASK 0x1FU
#define EV_RTC_HOURS_24H_VALUE_MASK 0x3FU
#define EV_RTC_WEEKDAY_MASK 0x07U
#define EV_RTC_DAY_MASK 0x3FU
#define EV_RTC_MONTH_MASK 0x1FU
#define EV_RTC_MONTH_CENTURY_MASK 0x80U
#define EV_RTC_YEAR_BASE 2000U
#define EV_RTC_UNIX_EPOCH_YEAR 1970U
#define EV_RTC_SECONDS_PER_MINUTE 60U
#define EV_RTC_MINUTES_PER_HOUR 60U
#define EV_RTC_HOURS_PER_DAY 24U
#define EV_RTC_SECONDS_PER_HOUR (EV_RTC_SECONDS_PER_MINUTE * EV_RTC_MINUTES_PER_HOUR)
#define EV_RTC_SECONDS_PER_DAY (EV_RTC_SECONDS_PER_HOUR * EV_RTC_HOURS_PER_DAY)
#define EV_RTC_FALLBACK_POLL_THRESHOLD_TICKS 2U

static uint8_t ev_rtc_actor_bcd_to_decimal(uint8_t bcd)
{
    return (uint8_t)(((uint8_t)(bcd >> 4U) * 10U) + (bcd & 0x0FU));
}

static uint8_t ev_rtc_actor_decode_hours(uint8_t raw_hours)
{
    if ((raw_hours & EV_RTC_HOURS_12H_MODE_MASK) != 0U) {
        uint8_t hours = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_hours & EV_RTC_HOURS_12H_VALUE_MASK));

        if ((raw_hours & EV_RTC_HOURS_PM_MASK) != 0U) {
            if (hours < 12U) {
                hours = (uint8_t)(hours + 12U);
            }
        } else if (hours == 12U) {
            hours = 0U;
        }

        return hours;
    }

    return ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_hours & EV_RTC_HOURS_24H_VALUE_MASK));
}

static bool ev_rtc_actor_is_leap_year(uint16_t year)
{
    if ((year % 400U) == 0U) {
        return true;
    }
    if ((year % 100U) == 0U) {
        return false;
    }

    return (year % 4U) == 0U;
}

static uint8_t ev_rtc_actor_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t k_days_in_month[] = {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
    };

    if ((month == 0U) || (month > 12U)) {
        return 0U;
    }
    if ((month == 2U) && ev_rtc_actor_is_leap_year(year)) {
        return 29U;
    }

    return k_days_in_month[month - 1U];
}

static bool ev_rtc_actor_payload_is_valid(const ev_time_payload_t *payload)
{
    if (payload == NULL) {
        return false;
    }
    if ((payload->month == 0U) || (payload->month > 12U) || (payload->weekday == 0U) || (payload->weekday > 7U) ||
        (payload->hours > 23U) || (payload->minutes > 59U) || (payload->seconds > 59U)) {
        return false;
    }
    if ((payload->day == 0U) || (payload->day > ev_rtc_actor_days_in_month(payload->year, payload->month))) {
        return false;
    }

    return true;
}

static uint32_t ev_rtc_actor_build_unix_time(const ev_time_payload_t *payload)
{
    uint32_t days = 0U;
    uint16_t year;
    uint8_t month;

    if (payload == NULL) {
        return 0U;
    }

    for (year = EV_RTC_UNIX_EPOCH_YEAR; year < payload->year; ++year) {
        days += ev_rtc_actor_is_leap_year(year) ? 366U : 365U;
    }

    for (month = 1U; month < payload->month; ++month) {
        days += ev_rtc_actor_days_in_month(payload->year, month);
    }

    days += (uint32_t)(payload->day - 1U);

    return (days * EV_RTC_SECONDS_PER_DAY) + ((uint32_t)payload->hours * EV_RTC_SECONDS_PER_HOUR) +
           ((uint32_t)payload->minutes * EV_RTC_SECONDS_PER_MINUTE) + (uint32_t)payload->seconds;
}

static bool ev_rtc_actor_payload_equals(const ev_time_payload_t *lhs, const ev_time_payload_t *rhs)
{
    return (lhs != NULL) && (rhs != NULL) && (memcmp(lhs, rhs, sizeof(*lhs)) == 0);
}

static ev_result_t ev_rtc_actor_publish_ready(ev_rtc_actor_ctx_t *ctx)
{
    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_RTC_READY, ACT_RTC);
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    return rc;
}

static ev_result_t ev_rtc_actor_publish_time_update(ev_rtc_actor_ctx_t *ctx, const ev_time_payload_t *payload)
{

    ev_msg_t msg = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    if ((ctx == NULL) || (payload == NULL) || (ctx->deliver == NULL) || (ctx->deliver_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, EV_TIME_UPDATED, ACT_RTC);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, payload, sizeof(*payload));
    }
    if (rc == EV_OK) {
        rc = ev_publish(&msg, ctx->deliver, ctx->deliver_context, NULL);
    }

    dispose_rc = ev_msg_dispose(&msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        rc = dispose_rc;
    }

    if (rc == EV_OK) {
        ++ctx->published_updates;
    }
    return rc;
}

static ev_result_t ev_rtc_actor_enable_square_wave(ev_rtc_actor_ctx_t *ctx)
{
    const uint8_t control = EV_RTC_CONTROL_1HZ_SQW;
    ev_i2c_status_t status;
    ev_result_t rc;

    if ((ctx == NULL) || (ctx->i2c_port == NULL) || (ctx->i2c_port->write_regs == NULL) || (ctx->irq_port == NULL) ||
        (ctx->irq_port->enable == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    status = ctx->i2c_port->write_regs(ctx->i2c_port->ctx,
                                       ctx->port_num,
                                       ctx->device_address_7bit,
                                       EV_RTC_REG_CONTROL,
                                       &control,
                                       1U);
    if (status != EV_I2C_OK) {
        return EV_ERR_STATE;
    }

    rc = ctx->irq_port->enable(ctx->irq_port->ctx, ctx->sqw_line_id, true);
    if (rc != EV_OK) {
        return rc;
    }

    return EV_OK;
}

static ev_result_t ev_rtc_actor_read_time_payload(ev_rtc_actor_ctx_t *ctx, ev_time_payload_t *payload)
{
    uint8_t raw_time[EV_RTC_TIME_BYTES] = {0};
    ev_i2c_status_t status;

    if ((ctx == NULL) || (payload == NULL) || (ctx->i2c_port == NULL) || (ctx->i2c_port->read_regs == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(payload, 0, sizeof(*payload));

    status = ctx->i2c_port->read_regs(ctx->i2c_port->ctx,
                                      ctx->port_num,
                                      ctx->device_address_7bit,
                                      EV_RTC_REG_SECONDS,
                                      raw_time,
                                      EV_RTC_TIME_BYTES);
    if (status != EV_I2C_OK) {
        return EV_ERR_STATE;
    }
    payload->seconds = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_time[0] & EV_RTC_SECONDS_MASK));
    payload->minutes = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_time[1] & EV_RTC_MINUTES_MASK));
    payload->hours = ev_rtc_actor_decode_hours(raw_time[2]);
    payload->weekday = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_time[3] & EV_RTC_WEEKDAY_MASK));
    payload->day = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_time[4] & EV_RTC_DAY_MASK));
    payload->month = ev_rtc_actor_bcd_to_decimal((uint8_t)(raw_time[5] & EV_RTC_MONTH_MASK));
    payload->year = (uint16_t)(EV_RTC_YEAR_BASE + ev_rtc_actor_bcd_to_decimal(raw_time[6]));
    if ((raw_time[5] & EV_RTC_MONTH_CENTURY_MASK) != 0U) {
        payload->year = (uint16_t)(payload->year + 100U);
    }

    if (!ev_rtc_actor_payload_is_valid(payload)) {
        return EV_OK;
    }

    payload->unix_time = ev_rtc_actor_build_unix_time(payload);
    return EV_OK;
}

static ev_result_t ev_rtc_actor_try_refresh_time(ev_rtc_actor_ctx_t *ctx, bool fallback_poll)
{
    ev_time_payload_t payload;
    ev_result_t rc;
    bool changed;

    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (fallback_poll) {
        ++ctx->fallback_polls;
    }

    rc = ev_rtc_actor_read_time_payload(ctx, &payload);
    if (rc != EV_OK) {
        ++ctx->read_failures;
        return EV_OK;
    }
    if (!ev_rtc_actor_payload_is_valid(&payload)) {
        ++ctx->read_failures;
        return EV_OK;
    }

    changed = !ctx->time_valid || !ev_rtc_actor_payload_equals(&ctx->last_time, &payload);
    {
        const bool was_valid = ctx->time_valid;
        ctx->last_time = payload;
        ctx->time_valid = true;
        if (!was_valid) {
            rc = ev_rtc_actor_publish_ready(ctx);
            if (rc != EV_OK) {
                return rc;
            }
        }
    }
    if (!changed) {
        return EV_OK;
    }

    rc = ev_rtc_actor_publish_time_update(ctx, &payload);
    if (rc != EV_OK) {
        return rc;
    }

    return EV_OK;
}

static ev_result_t ev_rtc_actor_handle_gpio_irq(ev_rtc_actor_ctx_t *ctx, const ev_msg_t *msg)
{
    const ev_irq_sample_t *sample;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    sample = (const ev_irq_sample_t *)ev_msg_payload_data(msg);
    if ((sample == NULL) || (ev_msg_payload_size(msg) != sizeof(*sample))) {
        return EV_ERR_CONTRACT;
    }

    if ((sample->line_id != ctx->sqw_line_id) || (sample->edge != EV_IRQ_EDGE_FALLING) || (sample->level != 0U)) {
        return EV_OK;
    }

    ++ctx->irq_samples_seen;
    ctx->ticks_since_last_irq = 0U;
    return ev_rtc_actor_try_refresh_time(ctx, false);
}

ev_result_t ev_rtc_actor_init(ev_rtc_actor_ctx_t *ctx,
                              ev_i2c_port_t *i2c_port,
                              ev_irq_port_t *irq_port,
                              ev_i2c_port_num_t port_num,
                              uint8_t device_address_7bit,
                              ev_irq_line_id_t sqw_line_id,
                              ev_delivery_fn_t deliver,
                              void *deliver_context)
{
    if ((ctx == NULL) || (i2c_port == NULL) || (i2c_port->read_regs == NULL) || (i2c_port->write_regs == NULL) ||
        (irq_port == NULL) || (irq_port->enable == NULL) || (deliver == NULL) || (deliver_context == NULL) ||
        (device_address_7bit > 0x7FU)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->i2c_port = i2c_port;
    ctx->irq_port = irq_port;
    ctx->port_num = port_num;
    ctx->device_address_7bit = device_address_7bit;
    ctx->sqw_line_id = sqw_line_id;
    ctx->deliver = deliver;
    ctx->deliver_context = deliver_context;
    return EV_OK;
}

ev_result_t ev_rtc_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_rtc_actor_ctx_t *ctx = (ev_rtc_actor_ctx_t *)actor_context;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        return ev_rtc_actor_try_refresh_time(ctx, false);

    case EV_MCP23008_READY: {
        ev_result_t rc = ev_rtc_actor_enable_square_wave(ctx);

        ctx->sqw_enabled = (rc == EV_OK);
        return EV_OK;
    }

    case EV_GPIO_IRQ:
        return ev_rtc_actor_handle_gpio_irq(ctx, msg);

    case EV_TICK_1S:
        ++ctx->ticks_since_last_irq;
        if (!ctx->sqw_enabled || (ctx->ticks_since_last_irq >= EV_RTC_FALLBACK_POLL_THRESHOLD_TICKS)) {
            return ev_rtc_actor_try_refresh_time(ctx, true);
        }
        return EV_OK;

    default:
        return EV_ERR_CONTRACT;
    }
}
