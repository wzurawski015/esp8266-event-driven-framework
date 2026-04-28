#include "fake_wdt_port.h"

#include <string.h>

static ev_result_t fake_wdt_enable(void *ctx, uint32_t timeout_ms)
{
    fake_wdt_port_t *fake = (fake_wdt_port_t *)ctx;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->enable_calls;
    fake->last_timeout_ms = timeout_ms;
    return fake->next_enable_result;
}

static ev_result_t fake_wdt_feed(void *ctx)
{
    fake_wdt_port_t *fake = (fake_wdt_port_t *)ctx;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->feed_calls;
    return fake->next_feed_result;
}

static ev_result_t fake_wdt_get_reset_reason(void *ctx, ev_reset_reason_t *out_reason)
{
    fake_wdt_port_t *fake = (fake_wdt_port_t *)ctx;

    if ((fake == NULL) || (out_reason == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->get_reset_reason_calls;
    *out_reason = fake->reset_reason;
    return fake->next_get_reset_reason_result;
}

static ev_result_t fake_wdt_is_supported(void *ctx, bool *out_supported)
{
    fake_wdt_port_t *fake = (fake_wdt_port_t *)ctx;

    if ((fake == NULL) || (out_supported == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    ++fake->is_supported_calls;
    *out_supported = fake->supported;
    return fake->next_is_supported_result;
}

void fake_wdt_port_init(fake_wdt_port_t *fake)
{
    if (fake == NULL) {
        return;
    }

    memset(fake, 0, sizeof(*fake));
    fake->next_enable_result = EV_OK;
    fake->next_feed_result = EV_OK;
    fake->next_is_supported_result = EV_OK;
    fake->next_get_reset_reason_result = EV_OK;
    fake->reset_reason = EV_RESET_REASON_UNKNOWN;
    fake->supported = true;
}

void fake_wdt_port_bind(ev_wdt_port_t *out_port, fake_wdt_port_t *fake)
{
    if (out_port == NULL) {
        return;
    }

    memset(out_port, 0, sizeof(*out_port));
    out_port->ctx = fake;
    out_port->enable = fake_wdt_enable;
    out_port->feed = fake_wdt_feed;
    out_port->get_reset_reason = fake_wdt_get_reset_reason;
    out_port->is_supported = fake_wdt_is_supported;
}
