#include "fake_clock_port.h"

#include <string.h>

static ev_result_t fake_clock_mono_now_us(void *ctx, ev_time_mono_us_t *out_now)
{
    fake_clock_port_t *fake = (fake_clock_port_t *)ctx;

    if ((fake == NULL) || (out_now == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->mono_calls;
    if (fake->next_mono_result != EV_OK) {
        return fake->next_mono_result;
    }

    *out_now = fake->mono_now_us;
    fake->mono_now_us += fake->mono_step_us;
    return EV_OK;
}

static ev_result_t fake_clock_wall_now_us(void *ctx, ev_time_wall_us_t *out_now)
{
    fake_clock_port_t *fake = (fake_clock_port_t *)ctx;

    if ((fake == NULL) || (out_now == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->wall_calls;
    if (fake->next_wall_result != EV_OK) {
        return fake->next_wall_result;
    }

    *out_now = fake->wall_now_us;
    return EV_OK;
}

static ev_result_t fake_clock_delay_ms(void *ctx, uint32_t delay_ms)
{
    fake_clock_port_t *fake = (fake_clock_port_t *)ctx;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->delay_calls;
    fake->last_delay_ms = delay_ms;
    if (fake->next_delay_result != EV_OK) {
        return fake->next_delay_result;
    }

    fake->mono_now_us += ((ev_time_mono_us_t)delay_ms * 1000ULL);
    return EV_OK;
}

void fake_clock_port_init(fake_clock_port_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
        fake->mono_step_us = 1U;
        fake->next_mono_result = EV_OK;
        fake->next_wall_result = EV_OK;
        fake->next_delay_result = EV_OK;
    }
}

void fake_clock_port_bind(ev_clock_port_t *out_port, fake_clock_port_t *fake)
{
    if (out_port != NULL) {
        out_port->ctx = fake;
        out_port->mono_now_us = fake_clock_mono_now_us;
        out_port->wall_now_us = fake_clock_wall_now_us;
        out_port->delay_ms = fake_clock_delay_ms;
    }
}
