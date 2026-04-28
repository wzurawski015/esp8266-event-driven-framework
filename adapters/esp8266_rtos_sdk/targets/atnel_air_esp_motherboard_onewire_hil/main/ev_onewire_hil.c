#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "rom/ets_sys.h"

#include "ev/esp8266_onewire_hil.h"
#include "ev/esp8266_port_adapters.h"

#define EV_HIL_ONEWIRE_TAG "ev_onewire_hil"
#define EV_HIL_DS18B20_CMD_SKIP_ROM 0xCCU
#define EV_HIL_DS18B20_CMD_CONVERT_T 0x44U
#define EV_HIL_DS18B20_CMD_READ_SCRATCHPAD 0xBEU
#define EV_HIL_DS18B20_SCRATCHPAD_BYTES 9U
#define EV_HIL_DS18B20_CONVERSION_WAIT_MS 750U
#define EV_HIL_DS18B20_DRAIN_SLICE_MS 10U
#define EV_HIL_IRQ_DRAIN_LIMIT 256U
#define EV_HIL_IRQ_FLOOD_STACK_WORDS 384U
#define EV_HIL_IRQ_FLOOD_HALF_PERIOD_US 250U

#if !defined(configSUPPORT_STATIC_ALLOCATION)
#define configSUPPORT_STATIC_ALLOCATION 0
#endif

typedef struct ev_hil_suite_result {
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
} ev_hil_suite_result_t;

typedef struct ev_hil_irq_flood_ctx {
    volatile bool run;
    volatile uint32_t toggles;
    int gpio;
} ev_hil_irq_flood_ctx_t;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticTask_t s_ev_hil_irq_flood_tcb;
static StackType_t s_ev_hil_irq_flood_stack[EV_HIL_IRQ_FLOOD_STACK_WORDS];
#endif
static ev_hil_irq_flood_ctx_t s_ev_hil_irq_flood_ctx;

static void ev_hil_pass(ev_hil_suite_result_t *result, const char *name)
{
    if (result != NULL) {
        ++result->passed;
    }
    ESP_LOGI(EV_HIL_ONEWIRE_TAG, "EV_HIL_CASE %s PASS", name);
}

static void ev_hil_fail(ev_hil_suite_result_t *result, const char *name, const char *reason)
{
    if (result != NULL) {
        ++result->failed;
    }
    ESP_LOGE(EV_HIL_ONEWIRE_TAG, "EV_HIL_CASE %s FAIL reason=%s", name, reason);
}

static bool ev_hil_gpio_is_valid(int gpio)
{
    return (gpio >= 0) && (gpio <= 15);
}

static ev_result_t ev_hil_configure_open_drain_gpio(int gpio)
{
    gpio_config_t cfg = {0};
    esp_err_t rc;

    if (!ev_hil_gpio_is_valid(gpio)) {
        return EV_ERR_INVALID_ARG;
    }

    cfg.pin_bit_mask = (uint64_t)1ULL << (unsigned)gpio;
    cfg.mode = GPIO_MODE_OUTPUT_OD;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;

    rc = gpio_config(&cfg);
    if (rc != ESP_OK) {
        return EV_ERR_STATE;
    }

    gpio_set_level((gpio_num_t)gpio, 1);
    return EV_OK;
}

static uint8_t ev_hil_ds18b20_crc8(const uint8_t *data, size_t data_len)
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
                crc ^= 0x8CU;
            }
            current = (uint8_t)(current >> 1U);
        }
    }

    return crc;
}

static uint32_t ev_hil_irq_drain(ev_irq_port_t *irq_port)
{
    uint32_t drained = 0U;

    if ((irq_port == NULL) || (irq_port->pop == NULL)) {
        return 0U;
    }

    while (drained < EV_HIL_IRQ_DRAIN_LIMIT) {
        ev_irq_sample_t sample = {0};
        ev_result_t rc = irq_port->pop(irq_port->ctx, &sample);
        if (rc == EV_ERR_EMPTY) {
            break;
        }
        if (rc != EV_OK) {
            break;
        }
        ++drained;
    }

    return drained;
}

static void ev_hil_irq_flood_task(void *arg)
{
    ev_hil_irq_flood_ctx_t *ctx = (ev_hil_irq_flood_ctx_t *)arg;
    uint8_t level = 0U;

    while ((ctx != NULL) && ctx->run) {
        gpio_set_level((gpio_num_t)ctx->gpio, level);
        level = (level == 0U) ? 1U : 0U;
        ++ctx->toggles;
        ets_delay_us(EV_HIL_IRQ_FLOOD_HALF_PERIOD_US);
    }

    if (ctx != NULL) {
        gpio_set_level((gpio_num_t)ctx->gpio, 1);
    }
    vTaskDelete(NULL);
}

static bool ev_hil_irq_flood_start(int gpio)
{
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    TaskHandle_t task;

    if (ev_hil_configure_open_drain_gpio(gpio) != EV_OK) {
        return false;
    }

    s_ev_hil_irq_flood_ctx.gpio = gpio;
    s_ev_hil_irq_flood_ctx.toggles = 0U;
    s_ev_hil_irq_flood_ctx.run = true;

    task = xTaskCreateStatic(ev_hil_irq_flood_task,
                             "ev_hil_ow_irq",
                             EV_HIL_IRQ_FLOOD_STACK_WORDS,
                             &s_ev_hil_irq_flood_ctx,
                             tskIDLE_PRIORITY + 1U,
                             s_ev_hil_irq_flood_stack,
                             &s_ev_hil_irq_flood_tcb);
    return task != NULL;
#else
    BaseType_t task_rc;

    if (ev_hil_configure_open_drain_gpio(gpio) != EV_OK) {
        return false;
    }

    s_ev_hil_irq_flood_ctx.gpio = gpio;
    s_ev_hil_irq_flood_ctx.toggles = 0U;
    s_ev_hil_irq_flood_ctx.run = true;

    /*
     * MISRA EXCEPTION: the ESP8266 RTOS SDK used by this HIL target may not
     * expose configSUPPORT_STATIC_ALLOCATION. Dynamic task creation is
     * permitted only during HIL bootstrap to start the auxiliary IRQ flood
     * task; the OneWire runtime under test remains zero-heap.
     */
    task_rc = xTaskCreate(ev_hil_irq_flood_task,
                          "ev_hil_ow_irq",
                          EV_HIL_IRQ_FLOOD_STACK_WORDS,
                          &s_ev_hil_irq_flood_ctx,
                          tskIDLE_PRIORITY + 1U,
                          NULL);
    return task_rc == pdPASS;
#endif
}
static void ev_hil_irq_flood_stop(void)
{
    s_ev_hil_irq_flood_ctx.run = false;
    vTaskDelay(pdMS_TO_TICKS(20U));
}

static void ev_hil_wait_and_drain(ev_irq_port_t *irq_port, uint32_t wait_ms, uint32_t *drained)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < wait_ms) {
        uint32_t slice_ms = wait_ms - elapsed_ms;
        if (slice_ms > EV_HIL_DS18B20_DRAIN_SLICE_MS) {
            slice_ms = EV_HIL_DS18B20_DRAIN_SLICE_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(slice_ms));
        if (drained != NULL) {
            *drained += ev_hil_irq_drain(irq_port);
        }
        elapsed_ms += slice_ms;
    }
}

static ev_onewire_status_t ev_hil_ds18b20_start_conversion(ev_onewire_port_t *port)
{
    ev_onewire_status_t status;

    status = port->reset(port->ctx);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    status = port->write_byte(port->ctx, EV_HIL_DS18B20_CMD_SKIP_ROM);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    return port->write_byte(port->ctx, EV_HIL_DS18B20_CMD_CONVERT_T);
}

static ev_onewire_status_t ev_hil_ds18b20_read_scratchpad(ev_onewire_port_t *port,
                                                          uint8_t scratchpad[EV_HIL_DS18B20_SCRATCHPAD_BYTES])
{
    ev_onewire_status_t status;
    size_t i;

    status = port->reset(port->ctx);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    status = port->write_byte(port->ctx, EV_HIL_DS18B20_CMD_SKIP_ROM);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }
    status = port->write_byte(port->ctx, EV_HIL_DS18B20_CMD_READ_SCRATCHPAD);
    if (status != EV_ONEWIRE_OK) {
        return status;
    }

    for (i = 0U; i < EV_HIL_DS18B20_SCRATCHPAD_BYTES; ++i) {
        status = port->read_byte(port->ctx, &scratchpad[i]);
        if (status != EV_ONEWIRE_OK) {
            return status;
        }
    }

    return EV_ONEWIRE_OK;
}

static void ev_hil_log_irq_diag(const char *stage)
{
    ev_esp8266_irq_diag_snapshot_t diag = {0};

    if (ev_esp8266_irq_get_diag(&diag) == EV_OK) {
        ESP_LOGI(EV_HIL_ONEWIRE_TAG,
                 "irq-diag:%s write=%u read=%u pending=%u dropped=%u high_watermark=%u mask=0x%08X",
                 stage,
                 (unsigned)diag.write_seq,
                 (unsigned)diag.read_seq,
                 (unsigned)diag.pending_samples,
                 (unsigned)diag.dropped_samples,
                 (unsigned)diag.high_watermark,
                 (unsigned)diag.active_gpio_mask);
    }
}

static void ev_hil_log_onewire_diag(const char *stage)
{
    ev_esp8266_onewire_diag_snapshot_t diag = {0};

    if (ev_esp8266_onewire_get_diag(&diag) == EV_OK) {
        ESP_LOGI(EV_HIL_ONEWIRE_TAG,
                 "onewire-diag:%s ops=%u crit=%u reset_crit=%u bit_crit=%u max_us=%u reset_max_us=%u bit_max_us=%u budget_violations=%u reset_low_us=%u bus_errors=%u",
                 stage,
                 (unsigned)diag.operations_started,
                 (unsigned)diag.critical_sections,
                 (unsigned)diag.reset_critical_sections,
                 (unsigned)diag.bit_critical_sections,
                 (unsigned)diag.max_critical_section_us,
                 (unsigned)diag.max_reset_critical_section_us,
                 (unsigned)diag.max_bit_critical_section_us,
                 (unsigned)diag.critical_section_budget_violations,
                 (unsigned)diag.max_reset_low_hold_us,
                 (unsigned)diag.bus_errors);
    }
}

static void ev_hil_test_ds18b20_read_irq_flood(const ev_esp8266_onewire_hil_config_t *cfg,
                                               ev_hil_suite_result_t *result)
{
    const char *const name = "ds18b20-read-irq-flood";
    ev_esp8266_irq_diag_snapshot_t before_irq = {0};
    ev_esp8266_irq_diag_snapshot_t after_irq = {0};
    ev_esp8266_onewire_diag_snapshot_t before_ow = {0};
    ev_esp8266_onewire_diag_snapshot_t after_ow = {0};
    uint32_t drained = 0U;
    uint32_t i;

    if ((cfg == NULL) || (cfg->onewire_port == NULL) || (cfg->onewire_port->reset == NULL) ||
        (cfg->onewire_port->write_byte == NULL) || (cfg->onewire_port->read_byte == NULL) ||
        (cfg->irq_port == NULL) || (cfg->irq_port->enable == NULL) || (cfg->irq_port->pop == NULL)) {
        ev_hil_fail(result, name, "required OneWire or IRQ port unavailable");
        return;
    }
    if (!ev_hil_gpio_is_valid(cfg->irq_flood_output_gpio)) {
        ev_hil_fail(result, name, "IRQ flood fixture GPIO disabled");
        return;
    }

    (void)cfg->irq_port->enable(cfg->irq_port->ctx, cfg->irq_flood_line_id, true);
    (void)ev_hil_irq_drain(cfg->irq_port);
    (void)ev_esp8266_irq_get_diag(&before_irq);
    (void)ev_esp8266_onewire_get_diag(&before_ow);

    if (!ev_hil_irq_flood_start(cfg->irq_flood_output_gpio)) {
        ev_hil_fail(result, name, "failed to start static IRQ flood task");
        return;
    }

    for (i = 0U; i < cfg->ds18b20_read_iterations; ++i) {
        uint8_t scratchpad[EV_HIL_DS18B20_SCRATCHPAD_BYTES] = {0};
        ev_onewire_status_t status;

        status = ev_hil_ds18b20_start_conversion(cfg->onewire_port);
        if (status != EV_ONEWIRE_OK) {
            ev_hil_irq_flood_stop();
            ev_hil_fail(result, name, "DS18B20 conversion command failed");
            return;
        }
        ev_hil_wait_and_drain(cfg->irq_port, EV_HIL_DS18B20_CONVERSION_WAIT_MS, &drained);

        status = ev_hil_ds18b20_read_scratchpad(cfg->onewire_port, scratchpad);
        if (status != EV_ONEWIRE_OK) {
            ev_hil_irq_flood_stop();
            ev_hil_fail(result, name, "DS18B20 scratchpad read failed");
            return;
        }
        if (ev_hil_ds18b20_crc8(scratchpad, EV_HIL_DS18B20_SCRATCHPAD_BYTES - 1U) !=
            scratchpad[EV_HIL_DS18B20_SCRATCHPAD_BYTES - 1U]) {
            ev_hil_irq_flood_stop();
            ev_hil_fail(result, name, "DS18B20 scratchpad CRC mismatch");
            return;
        }
        drained += ev_hil_irq_drain(cfg->irq_port);
    }

    ev_hil_irq_flood_stop();
    drained += ev_hil_irq_drain(cfg->irq_port);
    (void)ev_esp8266_irq_get_diag(&after_irq);
    (void)ev_esp8266_onewire_get_diag(&after_ow);

    if (after_irq.dropped_samples != before_irq.dropped_samples) {
        ev_hil_fail(result, name, "IRQ samples dropped during DS18B20 flood test");
    } else if (drained == 0U) {
        ev_hil_fail(result, name, "no IRQ samples observed; check flood fixture wiring");
    } else if (after_ow.critical_sections <= before_ow.critical_sections) {
        ev_hil_fail(result, name, "OneWire scheduler-protected timing sections were not measured");
    } else if ((cfg->max_reset_critical_section_us != 0U) &&
               (after_ow.max_reset_critical_section_us > cfg->max_reset_critical_section_us)) {
        ev_hil_fail(result, name, "OneWire reset scheduler-protected timing section budget exceeded");
    } else if ((cfg->max_bit_critical_section_us != 0U) &&
               (after_ow.max_bit_critical_section_us > cfg->max_bit_critical_section_us)) {
        ev_hil_fail(result, name, "OneWire bit scheduler-protected timing section budget exceeded");
    } else if (after_ow.critical_section_budget_violations != before_ow.critical_section_budget_violations) {
        ev_hil_fail(result, name, "OneWire scheduler-protected timing section budget violation observed");
    } else {
        ESP_LOGI(EV_HIL_ONEWIRE_TAG,
                 "%s drained=%u toggles=%u dropped_delta=0 max_crit_us=%u reset_max_us=%u bit_max_us=%u",
                 name,
                 (unsigned)drained,
                 (unsigned)s_ev_hil_irq_flood_ctx.toggles,
                 (unsigned)after_ow.max_critical_section_us,
                 (unsigned)after_ow.max_reset_critical_section_us,
                 (unsigned)after_ow.max_bit_critical_section_us);
        ev_hil_pass(result, name);
    }
}

ev_result_t ev_esp8266_onewire_irq_hil_run(const ev_esp8266_onewire_hil_config_t *cfg)
{
    ev_hil_suite_result_t result = {0};

    if ((cfg == NULL) || (cfg->ds18b20_read_iterations == 0U)) {
        return EV_ERR_INVALID_ARG;
    }

    ESP_LOGI(EV_HIL_ONEWIRE_TAG,
             "HIL start suite=%s board=%s ds18b20_reads=%u irq_gpio=%d line=%u",
             (cfg->suite_name != NULL) ? cfg->suite_name : "onewire-irq-hil",
             (cfg->board_tag != NULL) ? cfg->board_tag : "unknown",
             (unsigned)cfg->ds18b20_read_iterations,
             cfg->irq_flood_output_gpio,
             (unsigned)cfg->irq_flood_line_id);
    ev_hil_log_irq_diag("before");
    ev_hil_log_onewire_diag("before");

    ev_hil_test_ds18b20_read_irq_flood(cfg, &result);

    ev_hil_log_irq_diag("after");
    ev_hil_log_onewire_diag("after");
    ESP_LOGI(EV_HIL_ONEWIRE_TAG,
             "HIL summary passed=%u failed=%u skipped=%u",
             (unsigned)result.passed,
             (unsigned)result.failed,
             (unsigned)result.skipped);

    if ((result.failed == 0U) && (result.skipped == 0U)) {
        ESP_LOGI(EV_HIL_ONEWIRE_TAG, "EV_HIL_RESULT PASS failures=0 skipped=0");
        return EV_OK;
    }

    ESP_LOGE(EV_HIL_ONEWIRE_TAG,
             "EV_HIL_RESULT FAIL failures=%u skipped=%u",
             (unsigned)result.failed,
             (unsigned)result.skipped);
    return EV_ERR_STATE;
}
