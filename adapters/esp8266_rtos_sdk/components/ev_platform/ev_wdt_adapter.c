#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "ev/esp8266_port_adapters.h"

/**
 * MISRA EXCEPTION / SDK LIMITATION:
 * The ESP8266 RTOS SDK headers available to this repository do not expose a
 * verified, documented hardware-watchdog feed/configuration API.  Do not
 * invent vendor symbols here.  The portable Watchdog Actor can still enforce
 * health-gated policy through fakes and future targets may replace this stub
 * with a verified SDK implementation behind the same ev_wdt_port_t contract.
 */
typedef struct ev_esp8266_wdt_adapter_ctx {
    uint32_t enable_calls;
    uint32_t feed_calls;
    uint32_t last_timeout_ms;
    bool enabled;
} ev_esp8266_wdt_adapter_ctx_t;

static ev_esp8266_wdt_adapter_ctx_t g_ev_esp8266_wdt_ctx;

static ev_result_t ev_esp8266_wdt_enable_impl(void *ctx, uint32_t timeout_ms)
{
    ev_esp8266_wdt_adapter_ctx_t *adapter = (ev_esp8266_wdt_adapter_ctx_t *)ctx;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++adapter->enable_calls;
    adapter->last_timeout_ms = timeout_ms;
    adapter->enabled = false;
    return EV_ERR_UNSUPPORTED;
}

static ev_result_t ev_esp8266_wdt_feed_impl(void *ctx)
{
    ev_esp8266_wdt_adapter_ctx_t *adapter = (ev_esp8266_wdt_adapter_ctx_t *)ctx;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++adapter->feed_calls;
    return EV_ERR_UNSUPPORTED;
}

static ev_result_t ev_esp8266_wdt_get_reset_reason_impl(void *ctx, ev_reset_reason_t *out_reason)
{
    (void)ctx;

    if (out_reason == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    *out_reason = EV_RESET_REASON_UNKNOWN;
    return EV_OK;
}

static ev_result_t ev_esp8266_wdt_is_supported_impl(void *ctx, bool *out_supported)
{
    (void)ctx;

    if (out_supported == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    *out_supported = false;
    return EV_OK;
}

ev_result_t ev_esp8266_wdt_port_init(ev_wdt_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&g_ev_esp8266_wdt_ctx, 0, sizeof(g_ev_esp8266_wdt_ctx));
    out_port->ctx = &g_ev_esp8266_wdt_ctx;
    out_port->enable = ev_esp8266_wdt_enable_impl;
    out_port->feed = ev_esp8266_wdt_feed_impl;
    out_port->get_reset_reason = ev_esp8266_wdt_get_reset_reason_impl;
    out_port->is_supported = ev_esp8266_wdt_is_supported_impl;
    return EV_OK;
}
