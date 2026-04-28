#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp8266/gpio_register.h"
#include "rom/ets_sys.h"

#include "ev/compiler.h"
#include "ev/esp8266_port_adapters.h"

#define EV_ESP8266_IRQ_MAX_LINES 4U
#define EV_ESP8266_IRQ_RING_CAPACITY 64U
#define EV_ESP8266_IRQ_RING_MASK (EV_ESP8266_IRQ_RING_CAPACITY - 1U)

typedef struct {
    ev_irq_line_id_t line_id;
    uint8_t gpio_num;
    uint32_t gpio_mask;
    ev_gpio_irq_trigger_t trigger;
    uint8_t last_level;
    bool armed;
    bool configured;
} ev_esp8266_irq_line_t;

typedef struct {
    uint32_t gpio_status;
} ev_esp8266_irq_raw_sample_t;

typedef struct {
    ev_esp8266_irq_raw_sample_t ring[EV_ESP8266_IRQ_RING_CAPACITY];
    ev_esp8266_irq_line_t lines[EV_ESP8266_IRQ_MAX_LINES];
    volatile uint32_t write_seq;
    volatile uint32_t read_seq;
    volatile uint32_t dropped_samples;
    uint32_t decode_status;
    uint32_t high_watermark;
    uint32_t active_gpio_mask;
    uint32_t enabled_gpio_mask;
    uint32_t sleep_prepare_attempts;
    uint32_t sleep_prepare_failures;
    uint32_t sleep_prepared_gpio_mask;
    bool sleep_prepared;
    size_t line_count;
    bool configured;
    SemaphoreHandle_t wait_sem;
} ev_esp8266_irq_adapter_ctx_t;

static ev_esp8266_irq_adapter_ctx_t g_ev_irq_ctx;

EV_STATIC_ASSERT((EV_ESP8266_IRQ_RING_CAPACITY & EV_ESP8266_IRQ_RING_MASK) == 0U,
                 "IRQ ring capacity must stay a power of two");

static bool ev_esp8266_irq_gpio_is_valid(uint8_t gpio_num)
{
    return gpio_num <= 15U;
}

static bool ev_esp8266_irq_trigger_is_valid(ev_gpio_irq_trigger_t trigger)
{
    return (trigger == EV_GPIO_IRQ_TRIGGER_RISING) || (trigger == EV_GPIO_IRQ_TRIGGER_FALLING) ||
           (trigger == EV_GPIO_IRQ_TRIGGER_ANYEDGE);
}

static bool ev_esp8266_irq_pull_mode_is_valid(ev_gpio_irq_pull_mode_t pull_mode)
{
    return (pull_mode == EV_GPIO_IRQ_PULL_NONE) || (pull_mode == EV_GPIO_IRQ_PULL_UP);
}

static gpio_int_type_t ev_esp8266_gpio_intr_type_from_cfg(ev_gpio_irq_trigger_t trigger)
{
    switch (trigger) {
    case EV_GPIO_IRQ_TRIGGER_RISING:
        return GPIO_INTR_POSEDGE;
    case EV_GPIO_IRQ_TRIGGER_FALLING:
        return GPIO_INTR_NEGEDGE;
    case EV_GPIO_IRQ_TRIGGER_ANYEDGE:
    default:
        return GPIO_INTR_ANYEDGE;
    }
}

static ev_irq_edge_t ev_esp8266_irq_edge_from_sample(const ev_esp8266_irq_line_t *line, uint8_t level)
{
    if (line == NULL) {
        return EV_IRQ_EDGE_FALLING;
    }

    switch (line->trigger) {
    case EV_GPIO_IRQ_TRIGGER_RISING:
        return EV_IRQ_EDGE_RISING;
    case EV_GPIO_IRQ_TRIGGER_FALLING:
        return EV_IRQ_EDGE_FALLING;
    case EV_GPIO_IRQ_TRIGGER_ANYEDGE:
    default:
        if (level != line->last_level) {
            return (level != 0U) ? EV_IRQ_EDGE_RISING : EV_IRQ_EDGE_FALLING;
        }
        return (level != 0U) ? EV_IRQ_EDGE_RISING : EV_IRQ_EDGE_FALLING;
    }
}

static bool ev_esp8266_irq_line_configs_are_valid(const ev_gpio_irq_line_config_t *line_cfgs, size_t line_count)
{
    size_t i;
    size_t j;

    if ((line_cfgs == NULL) || (line_count == 0U) || (line_count > EV_ESP8266_IRQ_MAX_LINES)) {
        return false;
    }

    for (i = 0U; i < line_count; ++i) {
        if (!ev_esp8266_irq_gpio_is_valid(line_cfgs[i].gpio_num) ||
            !ev_esp8266_irq_trigger_is_valid(line_cfgs[i].trigger) ||
            !ev_esp8266_irq_pull_mode_is_valid(line_cfgs[i].pull_mode)) {
            return false;
        }

        for (j = i + 1U; j < line_count; ++j) {
            if ((line_cfgs[i].line_id == line_cfgs[j].line_id) || (line_cfgs[i].gpio_num == line_cfgs[j].gpio_num)) {
                return false;
            }
        }
    }

    return true;
}


static uint32_t ev_esp8266_irq_pending_count_unsafe(const ev_esp8266_irq_adapter_ctx_t *adapter)
{
    uint32_t pending;

    if (adapter == NULL) {
        return 0U;
    }

    pending = adapter->write_seq - adapter->read_seq;
    if (adapter->decode_status != 0U) {
        ++pending;
    }

    return pending;
}

static void ev_esp8266_irq_push_isr(ev_esp8266_irq_adapter_ctx_t *adapter, uint32_t gpio_status)
{
    BaseType_t higher_priority_woken = pdFALSE;
    uint32_t write_seq;
    uint32_t read_seq;

    if ((adapter == NULL) || (gpio_status == 0U)) {
        return;
    }

    write_seq = adapter->write_seq;
    read_seq = adapter->read_seq;
    if ((write_seq - read_seq) < EV_ESP8266_IRQ_RING_CAPACITY) {
        const uint32_t pending_after = (write_seq + 1U) - read_seq;
        adapter->ring[write_seq & EV_ESP8266_IRQ_RING_MASK].gpio_status = gpio_status;
        adapter->write_seq = write_seq + 1U;
        if (pending_after > adapter->high_watermark) {
            adapter->high_watermark = pending_after;
        }
        if (adapter->wait_sem != NULL) {
            (void)xSemaphoreGiveFromISR(adapter->wait_sem, &higher_priority_woken);
        }
    } else {
        ++adapter->dropped_samples;
    }

    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}


static void IRAM_ATTR ev_esp8266_irq_isr(void *arg)
{
    ev_esp8266_irq_adapter_ctx_t *adapter = (ev_esp8266_irq_adapter_ctx_t *)arg;
    uint32_t status;

    if (adapter == NULL) {
        return;
    }

    status = (uint32_t)(GPIO_REG_READ(GPIO_STATUS_ADDRESS) & GPIO_STATUS_INTERRUPT_DATA_MASK);
    status &= adapter->active_gpio_mask;
    if (status == 0U) {
        return;
    }

    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, status);
    ev_esp8266_irq_push_isr(adapter, status);
}


static ev_result_t ev_esp8266_irq_pop(void *ctx, ev_irq_sample_t *out_sample)
{
    ev_esp8266_irq_adapter_ctx_t *adapter = (ev_esp8266_irq_adapter_ctx_t *)ctx;
    ev_esp8266_irq_line_t *selected_line = NULL;
    uint32_t read_seq;
    size_t i;

    if ((adapter == NULL) || (out_sample == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!adapter->configured) {
        return EV_ERR_STATE;
    }

    portENTER_CRITICAL();

    if (adapter->decode_status == 0U) {
        read_seq = adapter->read_seq;
        if (read_seq == adapter->write_seq) {
            portEXIT_CRITICAL();
            return EV_ERR_EMPTY;
        }

        adapter->decode_status = adapter->ring[read_seq & EV_ESP8266_IRQ_RING_MASK].gpio_status & adapter->active_gpio_mask;
        adapter->read_seq = read_seq + 1U;
    }

    for (i = 0U; i < adapter->line_count; ++i) {
        ev_esp8266_irq_line_t *line = &adapter->lines[i];
        if ((adapter->decode_status & line->gpio_mask) != 0U) {
            adapter->decode_status &= (uint32_t)(~line->gpio_mask);
            selected_line = line;
            break;
        }
    }

    portEXIT_CRITICAL();

    if (selected_line == NULL) {
        return EV_ERR_EMPTY;
    }

    {
        const uint8_t level = (uint8_t)((gpio_get_level((gpio_num_t)selected_line->gpio_num) != 0) ? 1U : 0U);
        out_sample->line_id = selected_line->line_id;
        out_sample->edge = ev_esp8266_irq_edge_from_sample(selected_line, level);
        out_sample->level = level;
        out_sample->timestamp_us = (uint32_t)esp_timer_get_time();
        selected_line->last_level = level;
    }

    return EV_OK;
}


static ev_result_t ev_esp8266_irq_wait(void *ctx, uint32_t timeout_ms, bool *out_woken)
{
    ev_esp8266_irq_adapter_ctx_t *adapter = (ev_esp8266_irq_adapter_ctx_t *)ctx;
    TickType_t timeout_ticks;

    if ((adapter == NULL) || (out_woken == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!adapter->configured) {
        return EV_ERR_STATE;
    }

    portENTER_CRITICAL();
    if (ev_esp8266_irq_pending_count_unsafe(adapter) != 0U) {
        portEXIT_CRITICAL();
        *out_woken = true;
        return EV_OK;
    }
    portEXIT_CRITICAL();

    if (adapter->wait_sem == NULL) {
        return EV_ERR_STATE;
    }

    timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    if ((timeout_ms > 0U) && (timeout_ticks == 0)) {
        timeout_ticks = 1;
    }

    if (xSemaphoreTake(adapter->wait_sem, timeout_ticks) == pdTRUE) {
        *out_woken = true;
        return EV_OK;
    }

    *out_woken = false;
    return EV_OK;
}

static ev_result_t ev_esp8266_irq_get_stats(void *ctx, ev_irq_stats_t *out_stats)
{
    ev_esp8266_irq_adapter_ctx_t *adapter = (ev_esp8266_irq_adapter_ctx_t *)ctx;
    uint32_t write_seq;
    uint32_t read_seq;

    if ((adapter == NULL) || (out_stats == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!adapter->configured) {
        return EV_ERR_STATE;
    }

    portENTER_CRITICAL();
    write_seq = adapter->write_seq;
    read_seq = adapter->read_seq;
    out_stats->write_seq = write_seq;
    out_stats->read_seq = read_seq;
    out_stats->pending_samples = (write_seq - read_seq) + ((adapter->decode_status != 0U) ? 1U : 0U);
    out_stats->dropped_samples = adapter->dropped_samples;
    out_stats->high_watermark = adapter->high_watermark;
    portEXIT_CRITICAL();

    return EV_OK;
}

static ev_result_t ev_esp8266_irq_enable(void *ctx, ev_irq_line_id_t line_id, bool enabled)
{
    ev_esp8266_irq_adapter_ctx_t *adapter = (ev_esp8266_irq_adapter_ctx_t *)ctx;
    size_t i;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!adapter->configured) {
        return EV_ERR_STATE;
    }

    for (i = 0U; i < adapter->line_count; ++i) {
        ev_esp8266_irq_line_t *line = &adapter->lines[i];
        esp_err_t sdk_rc;
        gpio_int_type_t intr_type;

        if (line->line_id != line_id) {
            continue;
        }

        line->last_level = (uint8_t)((gpio_get_level((gpio_num_t)line->gpio_num) != 0) ? 1U : 0U);
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, (1UL << line->gpio_num));
        intr_type = enabled ? ev_esp8266_gpio_intr_type_from_cfg(line->trigger) : GPIO_INTR_DISABLE;
        sdk_rc = gpio_set_intr_type((gpio_num_t)line->gpio_num, intr_type);
        if (sdk_rc != ESP_OK) {
            return EV_ERR_STATE;
        }

        line->armed = enabled;
        if (enabled) {
            adapter->enabled_gpio_mask |= line->gpio_mask;
        } else {
            adapter->enabled_gpio_mask &= ~line->gpio_mask;
        }

        return EV_OK;
    }

    return EV_ERR_NOT_FOUND;
}

ev_result_t ev_esp8266_irq_port_init(ev_irq_port_t *out_port,
                                     const ev_gpio_irq_line_config_t *line_cfgs,
                                     size_t line_count)
{
    gpio_config_t sdk_cfg = {0};
    esp_err_t sdk_rc;
    size_t i;

    if ((out_port == NULL) || !ev_esp8266_irq_line_configs_are_valid(line_cfgs, line_count)) {
        return EV_ERR_INVALID_ARG;
    }

    {
        SemaphoreHandle_t wait_sem = g_ev_irq_ctx.wait_sem;
        memset(&g_ev_irq_ctx, 0, sizeof(g_ev_irq_ctx));
        g_ev_irq_ctx.wait_sem = wait_sem;
    }

    if (g_ev_irq_ctx.wait_sem == NULL) {
        /*
         * MISRA EXCEPTION: ESP8266 RTOS SDK lacks supported
         * configSUPPORT_STATIC_ALLOCATION for semaphore creation. Dynamic
         * allocation is permitted here ONLY during the boot-time IRQ adapter
         * initialization phase. Runtime ISR/ring hot paths never allocate and
         * use only the already-created wait semaphore for task notification.
         */
        g_ev_irq_ctx.wait_sem = xSemaphoreCreateBinary();
        if (g_ev_irq_ctx.wait_sem == NULL) {
            return EV_ERR_STATE;
        }
    }

    while (xSemaphoreTake(g_ev_irq_ctx.wait_sem, 0) == pdTRUE) {
    }

    for (i = 0U; i < line_count; ++i) {
        const ev_gpio_irq_line_config_t *src = &line_cfgs[i];
        ev_esp8266_irq_line_t *dst = &g_ev_irq_ctx.lines[i];
        const uint32_t gpio_mask = (uint32_t)(1UL << src->gpio_num);

        sdk_cfg.pin_bit_mask = gpio_mask;
        sdk_cfg.mode = GPIO_MODE_INPUT;
        sdk_cfg.pull_up_en = (src->pull_mode == EV_GPIO_IRQ_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        sdk_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        sdk_cfg.intr_type = GPIO_INTR_DISABLE;

        sdk_rc = gpio_config(&sdk_cfg);
        if (sdk_rc != ESP_OK) {
            return EV_ERR_STATE;
        }

        dst->line_id = src->line_id;
        dst->gpio_num = src->gpio_num;
        dst->gpio_mask = gpio_mask;
        dst->trigger = src->trigger;
        dst->last_level = (uint8_t)((gpio_get_level((gpio_num_t)src->gpio_num) != 0) ? 1U : 0U);
        dst->configured = true;
        g_ev_irq_ctx.active_gpio_mask |= dst->gpio_mask;
    }

    sdk_rc = gpio_isr_register(ev_esp8266_irq_isr, &g_ev_irq_ctx, 0, NULL);
    if (sdk_rc != ESP_OK) {
        return EV_ERR_STATE;
    }

    /* gpio_isr_register() only attaches the ISR. The Xtensa GPIO interrupt
     * source remains masked until it is explicitly unmasked, matching how the
     * SDK does it inside gpio_isr_handler_add(). Keep per-line trigger arming
     * separate via gpio_set_intr_type() in ev_esp8266_irq_enable(). */
    portENTER_CRITICAL();
    _xt_isr_unmask((uint32_t)(1UL << ETS_GPIO_INUM));
    portEXIT_CRITICAL();

    g_ev_irq_ctx.line_count = line_count;
    g_ev_irq_ctx.configured = true;

    out_port->ctx = &g_ev_irq_ctx;
    out_port->pop = ev_esp8266_irq_pop;
    out_port->enable = ev_esp8266_irq_enable;
    out_port->wait = ev_esp8266_irq_wait;
    out_port->get_stats = ev_esp8266_irq_get_stats;
    return EV_OK;
}

ev_result_t ev_esp8266_irq_get_diag(ev_esp8266_irq_diag_snapshot_t *out_snapshot)
{
    uint32_t write_seq;
    uint32_t read_seq;

    if (out_snapshot == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!g_ev_irq_ctx.configured) {
        return EV_ERR_STATE;
    }

    portENTER_CRITICAL();
    write_seq = g_ev_irq_ctx.write_seq;
    read_seq = g_ev_irq_ctx.read_seq;
    out_snapshot->write_seq = write_seq;
    out_snapshot->read_seq = read_seq;
    out_snapshot->pending_samples = (write_seq - read_seq) + ((g_ev_irq_ctx.decode_status != 0U) ? 1U : 0U);
    out_snapshot->dropped_samples = g_ev_irq_ctx.dropped_samples;
    out_snapshot->high_watermark = g_ev_irq_ctx.high_watermark;
    out_snapshot->active_gpio_mask = g_ev_irq_ctx.active_gpio_mask;
    out_snapshot->enabled_gpio_mask = g_ev_irq_ctx.enabled_gpio_mask;
    out_snapshot->sleep_prepare_attempts = g_ev_irq_ctx.sleep_prepare_attempts;
    out_snapshot->sleep_prepare_failures = g_ev_irq_ctx.sleep_prepare_failures;
    out_snapshot->sleep_prepared_gpio_mask = g_ev_irq_ctx.sleep_prepared_gpio_mask;
    out_snapshot->sleep_prepared = g_ev_irq_ctx.sleep_prepared;
    portEXIT_CRITICAL();

    return EV_OK;
}
static ev_result_t ev_esp8266_irq_restore_mask(ev_esp8266_irq_adapter_ctx_t *adapter, uint32_t gpio_mask)
{
    size_t i;
    ev_result_t result = EV_OK;

    if (adapter == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    for (i = 0U; i < adapter->line_count; ++i) {
        ev_esp8266_irq_line_t *line = &adapter->lines[i];
        if ((line->configured) && ((gpio_mask & line->gpio_mask) != 0U)) {
            const gpio_int_type_t intr_type = ev_esp8266_gpio_intr_type_from_cfg(line->trigger);
            if (gpio_set_intr_type((gpio_num_t)line->gpio_num, intr_type) != ESP_OK) {
                result = EV_ERR_STATE;
            } else {
                line->armed = true;
                adapter->enabled_gpio_mask |= line->gpio_mask;
            }
        }
    }

    adapter->sleep_prepared_gpio_mask &= (uint32_t)(~gpio_mask);
    adapter->sleep_prepared = adapter->sleep_prepared_gpio_mask != 0U;
    return result;
}

static uint32_t ev_esp8266_irq_pending_hw_status(const ev_esp8266_irq_adapter_ctx_t *adapter)
{
    uint32_t status;

    if (adapter == NULL) {
        return 0U;
    }

    status = (uint32_t)(GPIO_REG_READ(GPIO_STATUS_ADDRESS) & GPIO_STATUS_INTERRUPT_DATA_MASK);
    return status & adapter->active_gpio_mask;
}

ev_result_t ev_esp8266_irq_confirm_sleep_ready(void)
{
    uint32_t pending_samples;
    uint32_t pending_status;

    if (!g_ev_irq_ctx.configured) {
        return EV_ERR_STATE;
    }

    portENTER_CRITICAL();
    pending_samples = ev_esp8266_irq_pending_count_unsafe(&g_ev_irq_ctx);
    portEXIT_CRITICAL();
    pending_status = ev_esp8266_irq_pending_hw_status(&g_ev_irq_ctx);

    if ((pending_samples != 0U) || (pending_status != 0U)) {
        ++g_ev_irq_ctx.sleep_prepare_failures;
        return EV_ERR_STATE;
    }

    return EV_OK;
}

ev_result_t ev_esp8266_irq_abort_sleep_prepare(void)
{
    if (!g_ev_irq_ctx.configured) {
        return EV_ERR_STATE;
    }

    if (!g_ev_irq_ctx.sleep_prepared) {
        return EV_OK;
    }

    return ev_esp8266_irq_restore_mask(&g_ev_irq_ctx, g_ev_irq_ctx.sleep_prepared_gpio_mask);
}

ev_result_t ev_esp8266_irq_commit_sleep_prepare(void)
{
    if (!g_ev_irq_ctx.configured) {
        return EV_ERR_STATE;
    }

    if (!g_ev_irq_ctx.sleep_prepared) {
        return EV_OK;
    }

    if (ev_esp8266_irq_confirm_sleep_ready() != EV_OK) {
        return EV_ERR_STATE;
    }

    GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, g_ev_irq_ctx.active_gpio_mask);
    return EV_OK;
}

ev_result_t ev_esp8266_irq_prepare_for_sleep(void)
{
    uint32_t pending_samples;
    uint32_t armed_mask = 0U;
    size_t i;

    if (!g_ev_irq_ctx.configured) {
        return EV_ERR_STATE;
    }

    ++g_ev_irq_ctx.sleep_prepare_attempts;
    if (g_ev_irq_ctx.sleep_prepared) {
        return EV_OK;
    }

    portENTER_CRITICAL();
    pending_samples = ev_esp8266_irq_pending_count_unsafe(&g_ev_irq_ctx);
    portEXIT_CRITICAL();
    if ((pending_samples != 0U) || (ev_esp8266_irq_pending_hw_status(&g_ev_irq_ctx) != 0U)) {
        ++g_ev_irq_ctx.sleep_prepare_failures;
        return EV_ERR_STATE;
    }

    for (i = 0U; i < g_ev_irq_ctx.line_count; ++i) {
        ev_esp8266_irq_line_t *line = &g_ev_irq_ctx.lines[i];
        if (!line->armed) {
            continue;
        }
        if (gpio_set_intr_type((gpio_num_t)line->gpio_num, GPIO_INTR_DISABLE) != ESP_OK) {
            ++g_ev_irq_ctx.sleep_prepare_failures;
            (void)ev_esp8266_irq_restore_mask(&g_ev_irq_ctx, armed_mask);
            return EV_ERR_STATE;
        }
        armed_mask |= line->gpio_mask;
        line->armed = false;
        g_ev_irq_ctx.enabled_gpio_mask &= (uint32_t)(~line->gpio_mask);
        GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, line->gpio_mask);
    }

    portENTER_CRITICAL();
    pending_samples = ev_esp8266_irq_pending_count_unsafe(&g_ev_irq_ctx);
    portEXIT_CRITICAL();
    if ((pending_samples != 0U) || (ev_esp8266_irq_pending_hw_status(&g_ev_irq_ctx) != 0U)) {
        ++g_ev_irq_ctx.sleep_prepare_failures;
        (void)ev_esp8266_irq_restore_mask(&g_ev_irq_ctx, armed_mask);
        return EV_ERR_STATE;
    }

    g_ev_irq_ctx.sleep_prepared_gpio_mask = armed_mask;
    g_ev_irq_ctx.sleep_prepared = armed_mask != 0U;
    return EV_OK;
}