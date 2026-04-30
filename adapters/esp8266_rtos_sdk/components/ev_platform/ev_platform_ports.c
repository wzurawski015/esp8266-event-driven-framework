#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sleep.h"

#include "ev/esp8266_port_adapters.h"

static bool ev_uart_port_is_supported(ev_uart_port_num_t port)
{
    return port == 0U;
}

static bool ev_uart_data_bits_is_valid(uint8_t data_bits)
{
    return data_bits >= 5U && data_bits <= 8U;
}

static bool ev_uart_stop_bits_is_valid(uint8_t stop_bits)
{
    return stop_bits == 1U || stop_bits == 2U;
}

static ev_result_t ev_uart_validate_port(ev_uart_port_num_t port)
{
    return ev_uart_port_is_supported(port) ? EV_OK : EV_ERR_UNSUPPORTED;
}

static ev_result_t ev_uart_validate_config(ev_uart_port_num_t port, const ev_uart_config_t *cfg)
{
    ev_result_t port_check = ev_uart_validate_port(port);

    if (port_check != EV_OK) {
        return port_check;
    }

    if (cfg == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (cfg->baud_rate == 0U) {
        return EV_ERR_INVALID_ARG;
    }

    if (!ev_uart_data_bits_is_valid(cfg->data_bits)) {
        return EV_ERR_INVALID_ARG;
    }

    if (!ev_uart_stop_bits_is_valid(cfg->stop_bits)) {
        return EV_ERR_INVALID_ARG;
    }

    return EV_OK;
}

static ev_result_t ev_clock_mono_now_us(void *ctx, ev_time_mono_us_t *out_now)
{
    (void)ctx;

    if (out_now == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    /* ESP8266 RTOS SDK v3.4 exposes early monotonic time at millisecond
     * granularity through esp_log_early_timestamp(). Keep the public port unit
     * in microseconds, but document the effective 1 ms resolution explicitly. */
    *out_now = (ev_time_mono_us_t)esp_log_early_timestamp() * 1000ULL;
    return EV_OK;
}

static ev_result_t ev_clock_wall_now_us(void *ctx, ev_time_wall_us_t *out_now)
{
    (void)ctx;
    (void)out_now;
    return EV_ERR_UNSUPPORTED;
}

static ev_result_t ev_clock_delay_ms(void *ctx, uint32_t delay_ms)
{
    (void)ctx;
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    return EV_OK;
}

ev_result_t ev_esp8266_clock_port_init(ev_clock_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    out_port->ctx = NULL;
    out_port->mono_now_us = ev_clock_mono_now_us;
    out_port->wall_now_us = ev_clock_wall_now_us;
    out_port->delay_ms = ev_clock_delay_ms;
    return EV_OK;
}

static esp_log_level_t ev_map_log_level(ev_log_level_t level)
{
    switch (level) {
    case EV_LOG_DEBUG:
        return ESP_LOG_DEBUG;
    case EV_LOG_INFO:
        return ESP_LOG_INFO;
    case EV_LOG_WARN:
        return ESP_LOG_WARN;
    case EV_LOG_ERROR:
    case EV_LOG_FATAL:
    default:
        return ESP_LOG_ERROR;
    }
}

static ev_result_t ev_log_write_impl(void *ctx,
                                     ev_log_level_t level,
                                     const char *tag,
                                     const char *message,
                                     size_t message_len)
{
    (void)ctx;

    if (tag == NULL || message == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (message_len > (size_t)INT_MAX) {
        return EV_ERR_OUT_OF_RANGE;
    }

    esp_log_write(ev_map_log_level(level), tag, "%.*s", (int)message_len, message);
    return EV_OK;
}

static ev_result_t ev_log_flush_impl(void *ctx)
{
    (void)ctx;

    if (!uart_is_driver_installed(UART_NUM_0)) {
        return EV_OK;
    }

    if (uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(50)) != ESP_OK) {
        return EV_ERR_STATE;
    }
    return EV_OK;
}

ev_result_t ev_esp8266_log_port_init(ev_log_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    out_port->ctx = NULL;
    out_port->write = ev_log_write_impl;
    out_port->flush = ev_log_flush_impl;
    out_port->pending = NULL;
    return EV_OK;
}

const char *ev_reset_reason_to_cstr(ev_reset_reason_t reason)
{
    switch (reason) {
    case EV_RESET_REASON_POWER_ON:
        return "power_on";
    case EV_RESET_REASON_SOFTWARE:
        return "software";
    case EV_RESET_REASON_WATCHDOG:
        return "watchdog";
    case EV_RESET_REASON_DEEP_SLEEP:
        return "deep_sleep";
    case EV_RESET_REASON_EXTERNAL:
        return "external";
    case EV_RESET_REASON_BROWNOUT:
        return "brownout";
    case EV_RESET_REASON_UNKNOWN:
    default:
        return "unknown";
    }
}

static ev_result_t ev_reset_get_reason_impl(void *ctx, ev_reset_reason_t *out_reason)
{
    esp_reset_reason_t sdk_reason;
    (void)ctx;

    if (out_reason == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    sdk_reason = esp_reset_reason();
    switch (sdk_reason) {
    case ESP_RST_POWERON:
        *out_reason = EV_RESET_REASON_POWER_ON;
        break;
    case ESP_RST_EXT:
        *out_reason = EV_RESET_REASON_EXTERNAL;
        break;
    case ESP_RST_DEEPSLEEP:
        *out_reason = EV_RESET_REASON_DEEP_SLEEP;
        break;
    case ESP_RST_SW:
    case ESP_RST_FAST_SW:
    case ESP_RST_PANIC:
    case ESP_RST_SDIO:
        *out_reason = EV_RESET_REASON_SOFTWARE;
        break;
    case ESP_RST_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_INT_WDT:
        *out_reason = EV_RESET_REASON_WATCHDOG;
        break;
    case ESP_RST_BROWNOUT:
        *out_reason = EV_RESET_REASON_BROWNOUT;
        break;
    case ESP_RST_UNKNOWN:
    default:
        *out_reason = EV_RESET_REASON_UNKNOWN;
        break;
    }

    return EV_OK;
}

static ev_result_t ev_reset_restart_impl(void *ctx)
{
    (void)ctx;
    esp_restart();
    return EV_ERR_STATE;
}

ev_result_t ev_esp8266_reset_port_init(ev_reset_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    out_port->ctx = NULL;
    out_port->get_reason = ev_reset_get_reason_impl;
    out_port->restart = ev_reset_restart_impl;
    return EV_OK;
}

static uart_word_length_t ev_uart_word_length_from_bits(uint8_t data_bits)
{
    switch (data_bits) {
    case 5U:
        return UART_DATA_5_BITS;
    case 6U:
        return UART_DATA_6_BITS;
    case 7U:
        return UART_DATA_7_BITS;
    case 8U:
    default:
        return UART_DATA_8_BITS;
    }
}

static uart_stop_bits_t ev_uart_stop_bits_from_value(uint8_t stop_bits)
{
    switch (stop_bits) {
    case 2U:
        return UART_STOP_BITS_2;
    case 1U:
    default:
        return UART_STOP_BITS_1;
    }
}

static uart_parity_t ev_uart_parity_from_cfg(const ev_uart_config_t *cfg)
{
    if (cfg->parity_enable == false) {
        return UART_PARITY_DISABLE;
    }

    return cfg->parity_odd ? UART_PARITY_ODD : UART_PARITY_EVEN;
}

static ev_result_t ev_uart_init_impl(void *ctx,
                                     ev_uart_port_num_t port,
                                     const ev_uart_config_t *cfg)
{
    uart_config_t sdk_cfg = {0};
    (void)ctx;

    {
        ev_result_t validation = ev_uart_validate_config(port, cfg);
        if (validation != EV_OK) {
            return validation;
        }
    }

    sdk_cfg.baud_rate = (int)cfg->baud_rate;
    sdk_cfg.data_bits = ev_uart_word_length_from_bits(cfg->data_bits);
    sdk_cfg.parity = ev_uart_parity_from_cfg(cfg);
    sdk_cfg.stop_bits = ev_uart_stop_bits_from_value(cfg->stop_bits);
    sdk_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    sdk_cfg.rx_flow_ctrl_thresh = 0U;

    if (!uart_is_driver_installed((uart_port_t)port)) {
        if (uart_driver_install((uart_port_t)port, 256, 0, 0, NULL, 0) != ESP_OK) {
            return EV_ERR_STATE;
        }
    }

    if (uart_param_config((uart_port_t)port, &sdk_cfg) != ESP_OK) {
        return EV_ERR_STATE;
    }

    if (uart_flush((uart_port_t)port) != ESP_OK) {
        return EV_ERR_STATE;
    }

    return EV_OK;
}

static ev_result_t ev_uart_write_impl(void *ctx,
                                      ev_uart_port_num_t port,
                                      const void *data,
                                      size_t len,
                                      size_t *out_written)
{
    int written;
    (void)ctx;

    if (data == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (ev_uart_validate_port(port) != EV_OK) {
        return EV_ERR_UNSUPPORTED;
    }

    if (len == 0U) {
        if (out_written != NULL) {
            *out_written = 0U;
        }
        return EV_OK;
    }

    written = uart_write_bytes((uart_port_t)port, (const char *)data, len);
    if (written < 0) {
        return EV_ERR_STATE;
    }

    if (uart_wait_tx_done((uart_port_t)port, pdMS_TO_TICKS(200)) != ESP_OK) {
        return EV_ERR_STATE;
    }

    if (out_written != NULL) {
        *out_written = (size_t)written;
    }

    if ((size_t)written != len) {
        return EV_ERR_STATE;
    }

    return EV_OK;
}

static ev_result_t ev_uart_read_impl(void *ctx,
                                     ev_uart_port_num_t port,
                                     void *data,
                                     size_t len,
                                     size_t *out_read)
{
    int read_count;
    (void)ctx;

    if (data == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (ev_uart_validate_port(port) != EV_OK) {
        return EV_ERR_UNSUPPORTED;
    }

    if (len == 0U) {
        if (out_read != NULL) {
            *out_read = 0U;
        }
        return EV_OK;
    }

    read_count = uart_read_bytes((uart_port_t)port, (uint8_t *)data, (uint32_t)len, 0);
    if (read_count < 0) {
        return EV_ERR_STATE;
    }

    if (out_read != NULL) {
        *out_read = (size_t)read_count;
    }

    return EV_OK;
}

ev_result_t ev_esp8266_uart_port_init(ev_uart_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    out_port->ctx = NULL;
    out_port->init = ev_uart_init_impl;
    out_port->write = ev_uart_write_impl;
    out_port->read = ev_uart_read_impl;
    return EV_OK;
}


typedef struct ev_esp8266_system_adapter_ctx {
    ev_esp8266_system_sleep_profile_t sleep_profile;
    bool sleep_prepared;
} ev_esp8266_system_adapter_ctx_t;

static ev_esp8266_system_adapter_ctx_t g_ev_system_ctx;

static ev_result_t ev_system_abort_sleep_prepare_impl(void *ctx)
{
    ev_esp8266_system_adapter_ctx_t *adapter = (ev_esp8266_system_adapter_ctx_t *)ctx;
    ev_result_t rc = EV_OK;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (!adapter->sleep_prepared) {
        return EV_OK;
    }

    if ((adapter->sleep_profile.resource_mask & EV_ESP8266_SLEEP_RESOURCE_GPIO_IRQ) != 0U) {
        rc = ev_esp8266_irq_abort_sleep_prepare();
    }

    adapter->sleep_prepared = false;
    return rc;
}

static ev_result_t ev_system_prepare_for_sleep_impl(void *ctx, uint64_t duration_us)
{
    ev_esp8266_system_adapter_ctx_t *adapter = (ev_esp8266_system_adapter_ctx_t *)ctx;
    ev_result_t rc;
    bool irq_prepared = false;

    (void)duration_us;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (adapter->sleep_prepared) {
        return EV_OK;
    }

    if ((adapter->sleep_profile.resource_mask & EV_ESP8266_SLEEP_RESOURCE_GPIO_IRQ) != 0U) {
        rc = ev_esp8266_irq_prepare_for_sleep();
        if (rc != EV_OK) {
            return rc;
        }
        irq_prepared = true;
    }

    if ((adapter->sleep_profile.resource_mask & EV_ESP8266_SLEEP_RESOURCE_ONEWIRE0) != 0U) {
        rc = ev_esp8266_onewire_prepare_for_sleep();
        if (rc != EV_OK) {
            if (irq_prepared) {
                (void)ev_esp8266_irq_abort_sleep_prepare();
            }
            return rc;
        }
    }

    if ((adapter->sleep_profile.resource_mask & EV_ESP8266_SLEEP_RESOURCE_I2C0) != 0U) {
        rc = ev_esp8266_i2c_prepare_for_sleep(adapter->sleep_profile.i2c_port_num);
        if (rc != EV_OK) {
            if (irq_prepared) {
                (void)ev_esp8266_irq_abort_sleep_prepare();
            }
            return rc;
        }
    }

    if (irq_prepared) {
        rc = ev_esp8266_irq_confirm_sleep_ready();
        if (rc != EV_OK) {
            (void)ev_esp8266_irq_abort_sleep_prepare();
            return rc;
        }
    }

    adapter->sleep_prepared = true;
    return EV_OK;
}

static ev_result_t ev_system_deep_sleep_impl(void *ctx, uint64_t duration_us)
{
    ev_esp8266_system_adapter_ctx_t *adapter = (ev_esp8266_system_adapter_ctx_t *)ctx;
    ev_result_t rc;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if ((adapter->sleep_profile.resource_mask & EV_ESP8266_SLEEP_RESOURCE_GPIO_IRQ) != 0U) {
        rc = ev_esp8266_irq_commit_sleep_prepare();
        if (rc != EV_OK) {
            (void)ev_system_abort_sleep_prepare_impl(adapter);
            return rc;
        }
    }

    esp_deep_sleep(duration_us);
    return EV_ERR_STATE;
}

ev_result_t ev_esp8266_system_port_init_with_sleep_profile(
    ev_system_port_t *out_port,
    const ev_esp8266_system_sleep_profile_t *sleep_profile)
{
    ev_esp8266_system_sleep_profile_t profile = {0};

    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if (sleep_profile != NULL) {
        profile = *sleep_profile;
    }

    g_ev_system_ctx.sleep_profile = profile;
    g_ev_system_ctx.sleep_prepared = false;

    out_port->ctx = &g_ev_system_ctx;
    out_port->prepare_for_sleep = ev_system_prepare_for_sleep_impl;
    out_port->cancel_sleep_prepare = ev_system_abort_sleep_prepare_impl;
    out_port->deep_sleep = ev_system_deep_sleep_impl;
    return EV_OK;
}

ev_result_t ev_esp8266_system_port_init(ev_system_port_t *out_port)
{
    const ev_esp8266_system_sleep_profile_t profile = {0};
    return ev_esp8266_system_port_init_with_sleep_profile(out_port, &profile);
}
