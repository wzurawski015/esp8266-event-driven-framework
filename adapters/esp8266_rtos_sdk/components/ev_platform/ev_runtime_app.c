#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ev/demo_app.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/esp8266_runtime_app.h"

#define EV_RUNTIME_APP_DEFAULT_BAUD_RATE 115200U
#define EV_RUNTIME_APP_DEFAULT_TICK_MS 1000U
#define EV_RUNTIME_APP_MIN_WAIT_MS 1U
#define EV_RUNTIME_APP_MAX_WAIT_MS 100U

static void ev_runtime_app_logf(ev_log_port_t *log_port,
                                ev_log_level_t level,
                                const char *tag,
                                const char *fmt,
                                ...)
{
    char buffer[160];
    va_list ap;
    int len;

    if ((log_port == NULL) || (log_port->write == NULL) || (tag == NULL) || (fmt == NULL)) {
        return;
    }

    va_start(ap, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(buffer)) {
        len = (int)(sizeof(buffer) - 1U);
        buffer[len] = '\0';
    }

    (void)log_port->write(log_port->ctx, level, tag, buffer, (size_t)len);
}


static ev_result_t ev_runtime_app_now_ms(const ev_clock_port_t *clock_port, uint32_t *out_now_ms)
{
    ev_time_mono_us_t now_us = 0U;

    if ((clock_port == NULL) || (clock_port->mono_now_us == NULL) || (out_now_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    if (clock_port->mono_now_us(clock_port->ctx, &now_us) != EV_OK) {
        return EV_ERR_STATE;
    }

    *out_now_ms = (uint32_t)(now_us / 1000ULL);
    return EV_OK;
}

static ev_result_t ev_runtime_app_compute_wait_ms(const ev_demo_app_t *app,
                                                  const ev_clock_port_t *clock_port,
                                                  uint32_t *out_wait_ms)
{
    uint32_t now_ms = 0U;
    uint32_t next_deadline_ms;
    int32_t until_deadline_ms;

    if ((app == NULL) || (clock_port == NULL) || (out_wait_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    if (ev_demo_app_pending(app) > 0U) {
        *out_wait_ms = 0U;
        return EV_OK;
    }

    if (ev_runtime_app_now_ms(clock_port, &now_ms) != EV_OK) {
        return EV_ERR_STATE;
    }

    next_deadline_ms = app->next_tick_100ms_ms;
    if ((int32_t)(app->next_tick_ms - next_deadline_ms) < 0) {
        next_deadline_ms = app->next_tick_ms;
    }

    until_deadline_ms = (int32_t)(next_deadline_ms - now_ms);
    if (until_deadline_ms <= 0) {
        *out_wait_ms = 0U;
        return EV_OK;
    }

    *out_wait_ms = (uint32_t)until_deadline_ms;
    if (*out_wait_ms > EV_RUNTIME_APP_MAX_WAIT_MS) {
        *out_wait_ms = EV_RUNTIME_APP_MAX_WAIT_MS;
    }
    if (*out_wait_ms < EV_RUNTIME_APP_MIN_WAIT_MS) {
        *out_wait_ms = EV_RUNTIME_APP_MIN_WAIT_MS;
    }

    return EV_OK;
}

static ev_esp8266_system_sleep_profile_t ev_runtime_app_build_sleep_profile(
    const ev_demo_app_board_profile_t *board_profile)
{
    const ev_demo_app_board_profile_t *profile =
        (board_profile != NULL) ? board_profile : ev_demo_app_default_board_profile();
    ev_esp8266_system_sleep_profile_t sleep_profile = {0};

    sleep_profile.i2c_port_num = profile->i2c_port_num;
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_I2C0) != 0U) {
        sleep_profile.resource_mask |= EV_ESP8266_SLEEP_RESOURCE_I2C0;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_ONEWIRE0) != 0U) {
        sleep_profile.resource_mask |= EV_ESP8266_SLEEP_RESOURCE_ONEWIRE0;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_GPIO_IRQ) != 0U) {
        sleep_profile.resource_mask |= EV_ESP8266_SLEEP_RESOURCE_GPIO_IRQ;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_DEEP_SLEEP_WAKE_GPIO16) != 0U) {
        sleep_profile.resource_mask |= EV_ESP8266_SLEEP_RESOURCE_WAKE_GPIO16;
    }

    return sleep_profile;
}

static void ev_runtime_app_wait_for_work(const ev_demo_app_t *app,
                                         const ev_clock_port_t *clock_port,
                                         ev_irq_port_t *irq_port)
{
    uint32_t wait_ms = EV_RUNTIME_APP_MIN_WAIT_MS;

    if ((app == NULL) || (clock_port == NULL)) {
        return;
    }

    if (ev_runtime_app_compute_wait_ms(app, clock_port, &wait_ms) != EV_OK) {
        wait_ms = EV_RUNTIME_APP_MIN_WAIT_MS;
    }

    if (wait_ms == 0U) {
        return;
    }

    if ((irq_port != NULL) && (irq_port->wait != NULL)) {
        bool woke = false;

        if (irq_port->wait(irq_port->ctx, wait_ms, &woke) == EV_OK) {
            return;
        }
    }

    if (clock_port->delay_ms != NULL) {
        (void)clock_port->delay_ms(clock_port->ctx, wait_ms);
    }
}

static bool ev_runtime_app_config_is_valid(const ev_boot_diag_config_t *cfg)
{
    return (cfg != NULL) && (cfg->board_tag != NULL) && (cfg->board_name != NULL);
}

static ev_demo_app_t s_app;

void ev_esp8266_runtime_app_run(const ev_boot_diag_config_t *cfg,
                                ev_i2c_port_t *i2c_port,
                                ev_irq_port_t *irq_port,
                                ev_onewire_port_t *onewire_port,
                                ev_wdt_port_t *wdt_port,
                                ev_net_port_t *net_port,
                                const ev_demo_app_board_profile_t *board_profile)
{
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_reset_port_t reset_port;
    ev_uart_port_t uart_port;
    ev_system_port_t system_port;
    ev_esp8266_system_sleep_profile_t sleep_profile;
    ev_uart_config_t uart_cfg;
    ev_reset_reason_t reset_reason;
    ev_demo_app_config_t app_cfg = {0};
    ev_result_t rc;

    if (!ev_runtime_app_config_is_valid(cfg)) {
        return;
    }

    if (ev_esp8266_clock_port_init(&clock_port) != EV_OK) {
        return;
    }
    if (ev_esp8266_log_port_init(&log_port) != EV_OK) {
        return;
    }
    if (ev_esp8266_reset_port_init(&reset_port) != EV_OK) {
        return;
    }
    if (ev_esp8266_uart_port_init(&uart_port) != EV_OK) {
        return;
    }
    sleep_profile = ev_runtime_app_build_sleep_profile(board_profile);
    if (ev_esp8266_system_port_init_with_sleep_profile(&system_port, &sleep_profile) != EV_OK) {
        return;
    }

    uart_cfg.baud_rate = (cfg->uart_baud_rate == 0U) ? EV_RUNTIME_APP_DEFAULT_BAUD_RATE : cfg->uart_baud_rate;
    uart_cfg.data_bits = 8U;
    uart_cfg.stop_bits = 1U;
    uart_cfg.parity_enable = false;
    uart_cfg.parity_odd = false;

    if (uart_port.init(uart_port.ctx, cfg->uart_port, &uart_cfg) != EV_OK) {
        ev_runtime_app_logf(&log_port, EV_LOG_ERROR, cfg->board_tag, "uart adapter init failed");
        (void)log_port.flush(log_port.ctx);
        return;
    }

    if (reset_port.get_reason(reset_port.ctx, &reset_reason) != EV_OK) {
        reset_reason = EV_RESET_REASON_UNKNOWN;
    }

    ev_runtime_app_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "uart adapter ready");
    ev_runtime_app_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "framework event runtime boot");
    ev_runtime_app_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "board profile: %s", cfg->board_name);
    ev_runtime_app_logf(
        &log_port,
        EV_LOG_INFO,
        cfg->board_tag,
        "reset reason: %s",
        ev_reset_reason_to_cstr(reset_reason));
    (void)log_port.flush(log_port.ctx);

    app_cfg.app_tag = cfg->board_tag;
    app_cfg.board_name = cfg->board_name;
    app_cfg.tick_period_ms = (cfg->heartbeat_period_ms == 0U) ? EV_RUNTIME_APP_DEFAULT_TICK_MS : cfg->heartbeat_period_ms;
    app_cfg.clock_port = &clock_port;
    app_cfg.log_port = &log_port;
    app_cfg.i2c_port = i2c_port;
    app_cfg.irq_port = irq_port;
    app_cfg.onewire_port = onewire_port;
    app_cfg.system_port = &system_port;
    app_cfg.wdt_port = wdt_port;
    app_cfg.net_port = net_port;
    app_cfg.board_profile = board_profile;

    rc = ev_demo_app_init(&s_app, &app_cfg);
    if (rc == EV_OK) {
        rc = ev_demo_app_publish_boot(&s_app);
    }
    if (rc != EV_OK) {
        ev_runtime_app_logf(&log_port, EV_LOG_FATAL, cfg->board_tag, "demo runtime init failed rc=%d", (int)rc);
        (void)log_port.flush(log_port.ctx);
        (void)reset_port.restart(reset_port.ctx);
        return;
    }

    for (;;) {
        rc = ev_demo_app_poll(&s_app);
        if ((rc != EV_OK) && (rc != EV_ERR_PARTIAL)) {
            ev_runtime_app_logf(&log_port, EV_LOG_FATAL, cfg->board_tag, "demo runtime poll failed rc=%d", (int)rc);
            (void)log_port.flush(log_port.ctx);
            (void)reset_port.restart(reset_port.ctx);
            return;
        }

        ev_runtime_app_wait_for_work(&s_app, &clock_port, irq_port);
    }
}
