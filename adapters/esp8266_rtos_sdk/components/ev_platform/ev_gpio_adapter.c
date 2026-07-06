#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "ev/esp8266_port_adapters.h"

static const char *const k_ev_gpio_tag = "ev_gpio";

typedef struct ev_esp8266_gpio_ctx {
    uint32_t configured_mask;
} ev_esp8266_gpio_ctx_t;

static ev_esp8266_gpio_ctx_t g_ev_gpio_ctx;

static bool ev_esp8266_gpio_pin_is_valid(ev_gpio_num_t pin)
{
    return pin <= 16U;
}

static gpio_mode_t ev_esp8266_gpio_mode_from_cfg(const ev_gpio_config_t *cfg)
{
    if (cfg->direction == EV_GPIO_INPUT) {
        return GPIO_MODE_INPUT;
    }
    return cfg->open_drain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT;
}

static ev_result_t ev_esp8266_gpio_configure(void *ctx, ev_gpio_num_t pin, const ev_gpio_config_t *cfg)
{
    ev_esp8266_gpio_ctx_t *gpio = (ev_esp8266_gpio_ctx_t *)ctx;
    gpio_config_t sdk_cfg = {0};
    esp_err_t sdk_rc;

    if ((gpio == NULL) || (cfg == NULL) || !ev_esp8266_gpio_pin_is_valid(pin)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((cfg->direction != EV_GPIO_INPUT) && (cfg->direction != EV_GPIO_OUTPUT)) {
        return EV_ERR_INVALID_ARG;
    }

    sdk_cfg.pin_bit_mask = (1ULL << (unsigned)pin);
    sdk_cfg.mode = ev_esp8266_gpio_mode_from_cfg(cfg);
    sdk_cfg.pull_up_en = (cfg->pull == EV_GPIO_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    sdk_cfg.pull_down_en = (cfg->pull == EV_GPIO_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    sdk_cfg.intr_type = GPIO_INTR_DISABLE;

    sdk_rc = gpio_config(&sdk_cfg);
    if (sdk_rc != ESP_OK) {
        ESP_LOGE(k_ev_gpio_tag, "gpio_config failed gpio=%u rc=%d", (unsigned)pin, (int)sdk_rc);
        return EV_ERR_STATE;
    }
    if (cfg->direction == EV_GPIO_OUTPUT) {
        sdk_rc = gpio_set_level((gpio_num_t)pin, cfg->initial_high ? 1 : 0);
        if (sdk_rc != ESP_OK) {
            ESP_LOGE(k_ev_gpio_tag, "gpio_set_level initial failed gpio=%u rc=%d", (unsigned)pin, (int)sdk_rc);
            return EV_ERR_STATE;
        }
    }

    gpio->configured_mask |= (uint32_t)(1UL << pin);
    return EV_OK;
}

static ev_result_t ev_esp8266_gpio_write(void *ctx, ev_gpio_num_t pin, bool high)
{
    ev_esp8266_gpio_ctx_t *gpio = (ev_esp8266_gpio_ctx_t *)ctx;

    if ((gpio == NULL) || !ev_esp8266_gpio_pin_is_valid(pin)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((gpio->configured_mask & (uint32_t)(1UL << pin)) == 0U) {
        return EV_ERR_STATE;
    }
    if (gpio_set_level((gpio_num_t)pin, high ? 1 : 0) != ESP_OK) {
        return EV_ERR_STATE;
    }
    return EV_OK;
}

static ev_result_t ev_esp8266_gpio_read(void *ctx, ev_gpio_num_t pin, bool *out_high)
{
    ev_esp8266_gpio_ctx_t *gpio = (ev_esp8266_gpio_ctx_t *)ctx;

    if ((gpio == NULL) || (out_high == NULL) || !ev_esp8266_gpio_pin_is_valid(pin)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((gpio->configured_mask & (uint32_t)(1UL << pin)) == 0U) {
        return EV_ERR_STATE;
    }
    *out_high = gpio_get_level((gpio_num_t)pin) != 0;
    return EV_OK;
}

ev_result_t ev_esp8266_gpio_port_init(ev_gpio_port_t *out_port)
{
    if (out_port == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    g_ev_gpio_ctx.configured_mask = 0U;
    out_port->ctx = &g_ev_gpio_ctx;
    out_port->configure = ev_esp8266_gpio_configure;
    out_port->write = ev_esp8266_gpio_write;
    out_port->read = ev_esp8266_gpio_read;
    return EV_OK;
}
