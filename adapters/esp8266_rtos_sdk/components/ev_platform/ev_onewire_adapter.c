#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

#include "ev/esp8266_port_adapters.h"

#define EV_ESP8266_ONEWIRE_RESET_LOW_US 480U
#define EV_ESP8266_ONEWIRE_PRESENCE_SAMPLE_US 70U
#define EV_ESP8266_ONEWIRE_RESET_RELEASE_US 410U
#define EV_ESP8266_ONEWIRE_WRITE_1_LOW_US 6U
#define EV_ESP8266_ONEWIRE_WRITE_1_RELEASE_US 64U
#define EV_ESP8266_ONEWIRE_WRITE_0_LOW_US 60U
#define EV_ESP8266_ONEWIRE_WRITE_0_RELEASE_US 10U
#define EV_ESP8266_ONEWIRE_READ_INIT_LOW_US 6U
#define EV_ESP8266_ONEWIRE_READ_SAMPLE_US 9U
#define EV_ESP8266_ONEWIRE_READ_RELEASE_US 55U
#define EV_ESP8266_ONEWIRE_RELEASE_SETTLE_US 4U
#define EV_ESP8266_ONEWIRE_RESET_TIMING_SECTION_BUDGET_US 1250U
#define EV_ESP8266_ONEWIRE_BIT_TIMING_SECTION_BUDGET_US 120U

typedef struct ev_esp8266_onewire_adapter_ctx {
    int data_pin;
    bool configured;
    volatile bool busy;
    uint32_t operations_started;
    uint32_t sleep_prepare_attempts;
    uint32_t sleep_prepare_failures;
    uint32_t bus_errors;
    uint32_t critical_sections;
    uint32_t reset_critical_sections;
    uint32_t bit_critical_sections;
    uint32_t max_critical_section_us;
    uint32_t max_reset_critical_section_us;
    uint32_t max_bit_critical_section_us;
    uint32_t critical_section_budget_violations;
    uint32_t max_reset_low_hold_us;
} ev_esp8266_onewire_adapter_ctx_t;

static ev_esp8266_onewire_adapter_ctx_t g_ev_onewire0_ctx = {
    .data_pin = -1,
    .configured = false,
};

static uint32_t ev_esp8266_onewire_elapsed_us(int64_t start_us, int64_t end_us)
{
    if (end_us <= start_us) {
        return 0U;
    }
    if ((uint64_t)(end_us - start_us) > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)(end_us - start_us);
}

static void ev_esp8266_onewire_record_timing_section(ev_esp8266_onewire_adapter_ctx_t *ctx,
                                                     uint32_t elapsed_us,
                                                     bool reset_section)
{
    const uint32_t budget_us = reset_section ?
        EV_ESP8266_ONEWIRE_RESET_TIMING_SECTION_BUDGET_US :
        EV_ESP8266_ONEWIRE_BIT_TIMING_SECTION_BUDGET_US;

    if (ctx == NULL) {
        return;
    }

    ++ctx->critical_sections;
    if (elapsed_us > budget_us) {
        ++ctx->critical_section_budget_violations;
    }
    if (elapsed_us > ctx->max_critical_section_us) {
        ctx->max_critical_section_us = elapsed_us;
    }

    if (reset_section) {
        ++ctx->reset_critical_sections;
        if (elapsed_us > ctx->max_reset_critical_section_us) {
            ctx->max_reset_critical_section_us = elapsed_us;
        }
    } else {
        ++ctx->bit_critical_sections;
        if (elapsed_us > ctx->max_bit_critical_section_us) {
            ctx->max_bit_critical_section_us = elapsed_us;
        }
    }
}

static void ev_esp8266_onewire_record_reset_low_hold(ev_esp8266_onewire_adapter_ctx_t *ctx,
                                                     uint32_t elapsed_us)
{
    if (ctx == NULL) {
        return;
    }

    if (elapsed_us > ctx->max_reset_low_hold_us) {
        ctx->max_reset_low_hold_us = elapsed_us;
    }
}

static bool ev_esp8266_onewire_pin_is_valid(int pin)
{
    return (pin >= 0) && (pin <= 16);
}

static void ev_esp8266_onewire_drive_low(const ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    gpio_set_direction((gpio_num_t)ctx->data_pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level((gpio_num_t)ctx->data_pin, 0);
}

static void ev_esp8266_onewire_release_bus(const ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    gpio_set_direction((gpio_num_t)ctx->data_pin, GPIO_MODE_DEF_INPUT);
}

static bool ev_esp8266_onewire_sample_bus(const ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    return gpio_get_level((gpio_num_t)ctx->data_pin) != 0;
}

static bool ev_esp8266_onewire_ctx_is_valid(const ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    return (ctx != NULL) && ctx->configured && ev_esp8266_onewire_pin_is_valid(ctx->data_pin);
}

static __attribute__((noinline)) ev_onewire_status_t ev_esp8266_onewire_bus_error_slowpath(void)
{
    return EV_ONEWIRE_ERR_BUS;
}

static __attribute__((noinline)) ev_onewire_status_t ev_esp8266_onewire_no_device_slowpath(void)
{
    return EV_ONEWIRE_ERR_NO_DEVICE;
}

static void ev_esp8266_onewire_suspend_scheduler(void)
{
    vTaskSuspendAll();
}

static void ev_esp8266_onewire_resume_scheduler(void)
{
    (void)xTaskResumeAll();
}

static bool ev_esp8266_onewire_begin_operation_unlocked(ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    if (!ev_esp8266_onewire_ctx_is_valid(ctx) || ctx->busy) {
        return false;
    }

    ctx->busy = true;
    ++ctx->operations_started;
    return true;
}

static void ev_esp8266_onewire_end_operation_unlocked(ev_esp8266_onewire_adapter_ctx_t *ctx)
{
    if (ctx != NULL) {
        ctx->busy = false;
    }
}

static uint8_t ev_esp8266_onewire_bit_io_unlocked(ev_esp8266_onewire_adapter_ctx_t *ctx, uint8_t bit_value)
{
    uint8_t sampled = 0U;
    const int64_t start_us = esp_timer_get_time();
    int64_t end_us;

    ev_esp8266_onewire_drive_low(ctx);
    ets_delay_us(EV_ESP8266_ONEWIRE_READ_INIT_LOW_US);
    if (bit_value != 0U) {
        ev_esp8266_onewire_release_bus(ctx);
    }
    ets_delay_us(EV_ESP8266_ONEWIRE_READ_SAMPLE_US);
    sampled = ev_esp8266_onewire_sample_bus(ctx) ? 1U : 0U;
    ets_delay_us(EV_ESP8266_ONEWIRE_READ_RELEASE_US);
    ev_esp8266_onewire_release_bus(ctx);

    end_us = esp_timer_get_time();
    ev_esp8266_onewire_record_timing_section(ctx, ev_esp8266_onewire_elapsed_us(start_us, end_us), false);
    return sampled;
}

static ev_onewire_status_t ev_esp8266_onewire_reset(void *ctx)
{
    ev_esp8266_onewire_adapter_ctx_t *adapter = (ev_esp8266_onewire_adapter_ctx_t *)ctx;
    bool presence_high;
    bool stuck_low;
    int64_t reset_low_start_us;
    int64_t reset_low_end_us;
    int64_t reset_start_us;
    int64_t reset_end_us;

    /*
     * Protect the complete reset timing envelope from FreeRTOS task preemption
     * while leaving hardware interrupts enabled.  ISRs can still enqueue IRQ
     * samples into their ring buffer; context switches are deferred until
     * xTaskResumeAll(), preserving both 1-Wire timing and ISR latency.
     */
    ev_esp8266_onewire_suspend_scheduler();
    if (!ev_esp8266_onewire_begin_operation_unlocked(adapter)) {
        if (adapter != NULL) {
            ++adapter->bus_errors;
        }
        ev_esp8266_onewire_resume_scheduler();
        return ev_esp8266_onewire_bus_error_slowpath();
    }

    reset_start_us = esp_timer_get_time();
    reset_low_start_us = esp_timer_get_time();
    ev_esp8266_onewire_drive_low(adapter);
    ets_delay_us(EV_ESP8266_ONEWIRE_RESET_LOW_US);
    reset_low_end_us = esp_timer_get_time();
    ev_esp8266_onewire_record_reset_low_hold(
        adapter,
        ev_esp8266_onewire_elapsed_us(reset_low_start_us, reset_low_end_us));

    ev_esp8266_onewire_release_bus(adapter);
    ets_delay_us(EV_ESP8266_ONEWIRE_PRESENCE_SAMPLE_US);
    presence_high = ev_esp8266_onewire_sample_bus(adapter);
    ets_delay_us(EV_ESP8266_ONEWIRE_RESET_RELEASE_US);
    stuck_low = !ev_esp8266_onewire_sample_bus(adapter);

    reset_end_us = esp_timer_get_time();
    ev_esp8266_onewire_record_timing_section(
        adapter,
        ev_esp8266_onewire_elapsed_us(reset_start_us, reset_end_us),
        true);
    ev_esp8266_onewire_end_operation_unlocked(adapter);
    ev_esp8266_onewire_resume_scheduler();

    if (stuck_low) {
        ++adapter->bus_errors;
        return ev_esp8266_onewire_bus_error_slowpath();
    }
    if (presence_high) {
        return ev_esp8266_onewire_no_device_slowpath();
    }

    return EV_ONEWIRE_OK;
}

static ev_onewire_status_t ev_esp8266_onewire_write_byte(void *ctx, uint8_t value)
{
    ev_esp8266_onewire_adapter_ctx_t *adapter = (ev_esp8266_onewire_adapter_ctx_t *)ctx;
    uint8_t bit_index;

    ev_esp8266_onewire_suspend_scheduler();
    if (!ev_esp8266_onewire_begin_operation_unlocked(adapter)) {
        if (adapter != NULL) {
            ++adapter->bus_errors;
        }
        ev_esp8266_onewire_resume_scheduler();
        return ev_esp8266_onewire_bus_error_slowpath();
    }

    for (bit_index = 0U; bit_index < 8U; ++bit_index) {
        const int64_t start_us = esp_timer_get_time();
        int64_t end_us;

        ev_esp8266_onewire_drive_low(adapter);
        if ((value & 0x01U) != 0U) {
            ets_delay_us(EV_ESP8266_ONEWIRE_WRITE_1_LOW_US);
            ev_esp8266_onewire_release_bus(adapter);
            ets_delay_us(EV_ESP8266_ONEWIRE_WRITE_1_RELEASE_US);
        } else {
            ets_delay_us(EV_ESP8266_ONEWIRE_WRITE_0_LOW_US);
            ev_esp8266_onewire_release_bus(adapter);
            ets_delay_us(EV_ESP8266_ONEWIRE_WRITE_0_RELEASE_US);
        }

        end_us = esp_timer_get_time();
        ev_esp8266_onewire_record_timing_section(adapter, ev_esp8266_onewire_elapsed_us(start_us, end_us), false);
        value = (uint8_t)(value >> 1U);
    }

    ev_esp8266_onewire_end_operation_unlocked(adapter);
    ev_esp8266_onewire_resume_scheduler();
    return EV_ONEWIRE_OK;
}

static ev_onewire_status_t ev_esp8266_onewire_read_byte(void *ctx, uint8_t *out_value)
{
    ev_esp8266_onewire_adapter_ctx_t *adapter = (ev_esp8266_onewire_adapter_ctx_t *)ctx;
    uint8_t value = 0U;
    uint8_t bit_index;

    if (out_value == NULL) {
        if (adapter != NULL) {
            ++adapter->bus_errors;
        }
        return ev_esp8266_onewire_bus_error_slowpath();
    }

    ev_esp8266_onewire_suspend_scheduler();
    if (!ev_esp8266_onewire_begin_operation_unlocked(adapter)) {
        if (adapter != NULL) {
            ++adapter->bus_errors;
        }
        ev_esp8266_onewire_resume_scheduler();
        return ev_esp8266_onewire_bus_error_slowpath();
    }

    for (bit_index = 0U; bit_index < 8U; ++bit_index) {
        const uint8_t sampled = ev_esp8266_onewire_bit_io_unlocked(adapter, 1U);
        value = (uint8_t)(value >> 1U);
        if (sampled != 0U) {
            value = (uint8_t)(value | 0x80U);
        }
    }

    ev_esp8266_onewire_end_operation_unlocked(adapter);
    ev_esp8266_onewire_resume_scheduler();
    *out_value = value;
    return EV_ONEWIRE_OK;
}

ev_result_t ev_esp8266_onewire_get_diag(ev_esp8266_onewire_diag_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    out_snapshot->operations_started = g_ev_onewire0_ctx.operations_started;
    out_snapshot->sleep_prepare_attempts = g_ev_onewire0_ctx.sleep_prepare_attempts;
    out_snapshot->sleep_prepare_failures = g_ev_onewire0_ctx.sleep_prepare_failures;
    out_snapshot->bus_errors = g_ev_onewire0_ctx.bus_errors;
    out_snapshot->critical_sections = g_ev_onewire0_ctx.critical_sections;
    out_snapshot->reset_critical_sections = g_ev_onewire0_ctx.reset_critical_sections;
    out_snapshot->bit_critical_sections = g_ev_onewire0_ctx.bit_critical_sections;
    out_snapshot->max_critical_section_us = g_ev_onewire0_ctx.max_critical_section_us;
    out_snapshot->max_reset_critical_section_us = g_ev_onewire0_ctx.max_reset_critical_section_us;
    out_snapshot->max_bit_critical_section_us = g_ev_onewire0_ctx.max_bit_critical_section_us;
    out_snapshot->critical_section_budget_violations = g_ev_onewire0_ctx.critical_section_budget_violations;
    out_snapshot->max_reset_low_hold_us = g_ev_onewire0_ctx.max_reset_low_hold_us;
    out_snapshot->configured = g_ev_onewire0_ctx.configured;
    out_snapshot->busy = g_ev_onewire0_ctx.busy;
    out_snapshot->dq_high = ev_esp8266_onewire_ctx_is_valid(&g_ev_onewire0_ctx) ?
                                ev_esp8266_onewire_sample_bus(&g_ev_onewire0_ctx) : false;
    return EV_OK;
}

ev_result_t ev_esp8266_onewire_prepare_for_sleep(void)
{
    if (!ev_esp8266_onewire_ctx_is_valid(&g_ev_onewire0_ctx)) {
        return EV_ERR_STATE;
    }

    ++g_ev_onewire0_ctx.sleep_prepare_attempts;
    if (g_ev_onewire0_ctx.busy) {
        ++g_ev_onewire0_ctx.sleep_prepare_failures;
        return EV_ERR_STATE;
    }

    ev_esp8266_onewire_release_bus(&g_ev_onewire0_ctx);
    ets_delay_us(EV_ESP8266_ONEWIRE_RELEASE_SETTLE_US);
    if (!ev_esp8266_onewire_sample_bus(&g_ev_onewire0_ctx)) {
        ++g_ev_onewire0_ctx.sleep_prepare_failures;
        ++g_ev_onewire0_ctx.bus_errors;
        return EV_ERR_STATE;
    }

    return EV_OK;
}

ev_result_t ev_esp8266_onewire_port_init(ev_onewire_port_t *out_port, int data_pin)
{
    gpio_config_t sdk_cfg = {0};
    esp_err_t sdk_rc;

    if ((out_port == NULL) || !ev_esp8266_onewire_pin_is_valid(data_pin)) {
        return EV_ERR_INVALID_ARG;
    }

    sdk_cfg.pin_bit_mask = (1UL << (unsigned)data_pin);
    sdk_cfg.mode = GPIO_MODE_OUTPUT_OD;
    sdk_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    sdk_cfg.intr_type = GPIO_INTR_DISABLE;

    sdk_rc = gpio_config(&sdk_cfg);
    if (sdk_rc != ESP_OK) {
        return EV_ERR_STATE;
    }

    gpio_set_level((gpio_num_t)data_pin, 1);

    g_ev_onewire0_ctx.data_pin = data_pin;
    g_ev_onewire0_ctx.configured = true;
    g_ev_onewire0_ctx.busy = false;
    g_ev_onewire0_ctx.operations_started = 0U;
    g_ev_onewire0_ctx.sleep_prepare_attempts = 0U;
    g_ev_onewire0_ctx.sleep_prepare_failures = 0U;
    g_ev_onewire0_ctx.bus_errors = 0U;
    g_ev_onewire0_ctx.critical_sections = 0U;
    g_ev_onewire0_ctx.reset_critical_sections = 0U;
    g_ev_onewire0_ctx.bit_critical_sections = 0U;
    g_ev_onewire0_ctx.max_critical_section_us = 0U;
    g_ev_onewire0_ctx.max_reset_critical_section_us = 0U;
    g_ev_onewire0_ctx.max_bit_critical_section_us = 0U;
    g_ev_onewire0_ctx.critical_section_budget_violations = 0U;
    g_ev_onewire0_ctx.max_reset_low_hold_us = 0U;

    out_port->ctx = &g_ev_onewire0_ctx;
    out_port->reset = ev_esp8266_onewire_reset;
    out_port->write_byte = ev_esp8266_onewire_write_byte;
    out_port->read_byte = ev_esp8266_onewire_read_byte;

    return EV_OK;
}
