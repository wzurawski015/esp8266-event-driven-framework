#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/esp8266_boot_diag.h"
#include "ev/esp8266_port_adapters.h"

#define EV_BOOT_DIAG_DEFAULT_BAUD_RATE 115200U
#define EV_BOOT_DIAG_DEFAULT_HEARTBEAT_MS 1000U

static void ev_boot_diag_logf(ev_log_port_t *log_port,
                              ev_log_level_t level,
                              const char *tag,
                              const char *fmt,
                              ...)
{
    char buffer[160];
    va_list ap;
    int len;

    if (log_port == NULL || log_port->write == NULL || tag == NULL || fmt == NULL) {
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

static uint32_t ev_boot_diag_mono_us_to_ms(ev_time_mono_us_t mono_now_us)
{
    return (uint32_t)((uint64_t)mono_now_us / 1000ULL);
}

static bool ev_boot_diag_config_is_valid(const ev_boot_diag_config_t *cfg)
{
    return cfg != NULL && cfg->board_tag != NULL && cfg->board_name != NULL;
}

void ev_esp8266_boot_diag_run(const ev_boot_diag_config_t *cfg)
{
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_reset_port_t reset_port;
    ev_uart_port_t uart_port;
    ev_uart_config_t uart_cfg;
    ev_reset_reason_t reset_reason;
    ev_time_mono_us_t mono_now_us;
    ev_uart_port_num_t uart_port_num;
    uint32_t heartbeat = 0U;
    uint32_t mono_now_ms = 0U;
    uint32_t heartbeat_period_ms;

    if (!ev_boot_diag_config_is_valid(cfg)) {
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

    uart_port_num = cfg->uart_port;
    heartbeat_period_ms = cfg->heartbeat_period_ms == 0U
        ? EV_BOOT_DIAG_DEFAULT_HEARTBEAT_MS
        : cfg->heartbeat_period_ms;

    uart_cfg.baud_rate = cfg->uart_baud_rate == 0U
        ? EV_BOOT_DIAG_DEFAULT_BAUD_RATE
        : cfg->uart_baud_rate;
    uart_cfg.data_bits = 8U;
    uart_cfg.stop_bits = 1U;
    uart_cfg.parity_enable = false;
    uart_cfg.parity_odd = false;

    if (uart_port.init(uart_port.ctx, uart_port_num, &uart_cfg) != EV_OK) {
        ev_boot_diag_logf(&log_port, EV_LOG_ERROR, cfg->board_tag, "uart adapter init failed");
        (void)log_port.flush(log_port.ctx);
        return;
    }

    if (reset_port.get_reason(reset_port.ctx, &reset_reason) != EV_OK) {
        reset_reason = EV_RESET_REASON_UNKNOWN;
    }

    if (clock_port.mono_now_us(clock_port.ctx, &mono_now_us) != EV_OK) {
        mono_now_us = 0U;
    }
    mono_now_ms = ev_boot_diag_mono_us_to_ms(mono_now_us);

    ev_boot_diag_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "uart adapter ready");
    ev_boot_diag_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "framework boot");
    ev_boot_diag_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "board profile: %s", cfg->board_name);
    ev_boot_diag_logf(&log_port,
                      EV_LOG_INFO,
                      cfg->board_tag,
                      "reset reason: %s",
                      ev_reset_reason_to_cstr(reset_reason));
    ev_boot_diag_logf(&log_port, EV_LOG_INFO, cfg->board_tag, "mono_now_ms=%u", (unsigned)mono_now_ms);
    ev_boot_diag_logf(&log_port,
                      EV_LOG_INFO,
                      cfg->board_tag,
                      "clock port contract size: %u",
                      (unsigned)sizeof(ev_clock_port_t));
    (void)log_port.flush(log_port.ctx);

    for (;;) {
        if (clock_port.mono_now_us(clock_port.ctx, &mono_now_us) != EV_OK) {
            mono_now_us = 0U;
        }
        mono_now_ms = ev_boot_diag_mono_us_to_ms(mono_now_us);

        ev_boot_diag_logf(&log_port,
                          EV_LOG_INFO,
                          cfg->board_tag,
                          "heartbeat=%u mono_now_ms=%u",
                          (unsigned)heartbeat++,
                          (unsigned)mono_now_ms);
        (void)log_port.flush(log_port.ctx);
        (void)clock_port.delay_ms(clock_port.ctx, heartbeat_period_ms);
    }
}
