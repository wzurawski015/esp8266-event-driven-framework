#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "rom/ets_sys.h"

#include "ev/esp8266_i2c_hil.h"
#include "ev/esp8266_port_adapters.h"

#define EV_HIL_I2C_TAG "ev_i2c_hil"
#define EV_HIL_OLED_CONTROL_CMD 0x00U
#define EV_HIL_OLED_CONTROL_DATA 0x40U
#define EV_HIL_OLED_WIDTH 72U
#define EV_HIL_OLED_PAGE_COUNT 5U
#define EV_HIL_OLED_DATA_CHUNK_BYTES 32U
#define EV_HIL_IRQ_DRAIN_LIMIT 256U
#define EV_HIL_IRQ_FLOOD_STACK_WORDS 384U
#define EV_HIL_IRQ_FLOOD_HALF_PERIOD_US 250U

#define EV_HIL_MCP23008_REG_IODIR 0x00U
#define EV_HIL_MCP23008_REG_GPIO 0x09U
#define EV_HIL_MCP23008_REG_OLAT 0x0AU

#if !defined(configSUPPORT_STATIC_ALLOCATION)
#define configSUPPORT_STATIC_ALLOCATION 0
#endif

#ifndef EV_BOARD_I2C_SDA_GPIO
#define EV_BOARD_I2C_SDA_GPIO (-1)
#endif
#ifndef EV_BOARD_I2C_SCL_GPIO
#define EV_BOARD_I2C_SCL_GPIO (-1)
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

static const char *ev_hil_status_name(ev_i2c_status_t status)
{
    switch (status) {
    case EV_I2C_OK:
        return "OK";
    case EV_I2C_ERR_TIMEOUT:
        return "TIMEOUT";
    case EV_I2C_ERR_NACK:
        return "NACK";
    case EV_I2C_ERR_BUS_LOCKED:
    default:
        return "BUS_LOCKED";
    }
}

static void ev_hil_pass(ev_hil_suite_result_t *result, const char *name)
{
    if (result != NULL) {
        ++result->passed;
    }
    ESP_LOGI(EV_HIL_I2C_TAG, "EV_HIL_CASE %s PASS", name);
}

static void ev_hil_fail(ev_hil_suite_result_t *result, const char *name, const char *reason)
{
    if (result != NULL) {
        ++result->failed;
    }
    ESP_LOGE(EV_HIL_I2C_TAG, "EV_HIL_CASE %s FAIL reason=%s", name, reason);
}

static void ev_hil_skip(ev_hil_suite_result_t *result, const char *name, const char *reason)
{
    if (result != NULL) {
        ++result->skipped;
    }
    ESP_LOGW(EV_HIL_I2C_TAG, "EV_HIL_CASE %s SKIP reason=%s", name, reason);
}

static bool ev_hil_gpio_is_valid(int gpio)
{
    return (gpio >= 0) && (gpio <= 15);
}

static int ev_hil_gpio_level_or_minus_one(int gpio)
{
    if (!ev_hil_gpio_is_valid(gpio)) {
        return -1;
    }
    return gpio_get_level((gpio_num_t)gpio);
}

static void ev_hil_log_fault_fixture_levels(const char *name,
                                             const char *stage,
                                             int fault_gpio,
                                             int sda_gpio,
                                             int scl_gpio)
{
    ESP_LOGI(EV_HIL_I2C_TAG,
             "fault-fixture:%s:%s fault_gpio=%d fault_level=%d sda_gpio=%d sda_level=%d scl_gpio=%d scl_level=%d",
             (name != NULL) ? name : "unknown",
             (stage != NULL) ? stage : "unknown",
             fault_gpio,
             ev_hil_gpio_level_or_minus_one(fault_gpio),
             sda_gpio,
             ev_hil_gpio_level_or_minus_one(sda_gpio),
             scl_gpio,
             ev_hil_gpio_level_or_minus_one(scl_gpio));
}

static const char *ev_hil_fault_reason_for_sda(ev_i2c_status_t stuck_status, ev_i2c_status_t recovery_status)
{
    if ((stuck_status == EV_I2C_OK) && (recovery_status == EV_I2C_OK)) {
        return "fault GPIO did not pull SDA low; check fixture coupling";
    }
    return "SDA fault was not contained or bus did not recover after release";
}

static const char *ev_hil_fault_reason_for_scl(ev_i2c_status_t stuck_status, ev_i2c_status_t recovery_status)
{
    if ((stuck_status == EV_I2C_OK) && (recovery_status == EV_I2C_OK)) {
        return "fault GPIO did not pull SCL low; check fixture coupling";
    }
    return "SCL fault did not timeout or bus did not recover after release";
}

static bool ev_hil_i2c_port_is_valid(const ev_i2c_port_t *port)
{
    return (port != NULL) && (port->ctx != NULL) && (port->write_stream != NULL) && (port->write_regs != NULL) &&
           (port->read_regs != NULL);
}

static uint32_t ev_hil_free_heap(void)
{
    return (uint32_t)esp_get_free_heap_size();
}

static void ev_hil_heap_gate(ev_hil_suite_result_t *result, const char *name, uint32_t before_heap)
{
    const uint32_t after_heap = ev_hil_free_heap();

    ESP_LOGI(EV_HIL_I2C_TAG,
             "heap-gate: %s before=%u after=%u delta=%d",
             name,
             (unsigned)before_heap,
             (unsigned)after_heap,
             (int)after_heap - (int)before_heap);
    if (after_heap < before_heap) {
        ev_hil_fail(result, name, "heap decreased during zero-heap I2C transaction loop");
    }
}

static ev_i2c_status_t ev_hil_i2c_write_stream(const ev_esp8266_i2c_hil_config_t *cfg,
                                                uint8_t addr,
                                                const uint8_t *data,
                                                size_t data_len)
{
    return cfg->i2c_port->write_stream(cfg->i2c_port->ctx, cfg->i2c_port_num, addr, data, data_len);
}

static ev_i2c_status_t ev_hil_i2c_write_regs(const ev_esp8266_i2c_hil_config_t *cfg,
                                              uint8_t addr,
                                              uint8_t reg,
                                              const uint8_t *data,
                                              size_t data_len)
{
    return cfg->i2c_port->write_regs(cfg->i2c_port->ctx, cfg->i2c_port_num, addr, reg, data, data_len);
}

static ev_i2c_status_t ev_hil_i2c_read_regs(const ev_esp8266_i2c_hil_config_t *cfg,
                                             uint8_t addr,
                                             uint8_t reg,
                                             uint8_t *data,
                                             size_t data_len)
{
    return cfg->i2c_port->read_regs(cfg->i2c_port->ctx, cfg->i2c_port_num, addr, reg, data, data_len);
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

static void ev_hil_fault_gpio_release(int gpio)
{
    if (ev_hil_gpio_is_valid(gpio)) {
        gpio_set_level((gpio_num_t)gpio, 1);
    }
}

static void ev_hil_fault_gpio_drive_low(int gpio)
{
    if (ev_hil_gpio_is_valid(gpio)) {
        gpio_set_level((gpio_num_t)gpio, 0);
    }
}

static bool ev_hil_fault_gpio_prepare(ev_hil_suite_result_t *result, const char *name, int gpio)
{
    ev_result_t rc;

    if (!ev_hil_gpio_is_valid(gpio)) {
        ev_hil_skip(result, name, "fixture GPIO disabled");
        return false;
    }

    rc = ev_hil_configure_open_drain_gpio(gpio);
    if (rc != EV_OK) {
        ev_hil_fail(result, name, "failed to configure fault-injection GPIO");
        return false;
    }

    return true;
}

static bool ev_hil_i2c_expect_ok(ev_hil_suite_result_t *result,
                                 const char *name,
                                 ev_i2c_status_t status,
                                 uint32_t iteration)
{
    if (status == EV_I2C_OK) {
        return true;
    }

    ESP_LOGE(EV_HIL_I2C_TAG,
             "%s iteration=%u status=%s",
             name,
             (unsigned)iteration,
             ev_hil_status_name(status));
    ev_hil_fail(result, name, "unexpected I2C status");
    return false;
}

static void ev_hil_log_i2c_diag(const char *stage, ev_i2c_port_num_t port_num)
{
    ev_esp8266_i2c_diag_snapshot_t diag = {0};

    if (ev_esp8266_i2c_get_diag(port_num, &diag) == EV_OK) {
        ESP_LOGI(EV_HIL_I2C_TAG,
                 "i2c-diag:%s started=%u failed=%u nacks=%u timeouts=%u locked=%u recoveries=%u recovery_failures=%u",
                 stage,
                 (unsigned)diag.transactions_started,
                 (unsigned)diag.transactions_failed,
                 (unsigned)diag.nacks,
                 (unsigned)diag.timeouts,
                 (unsigned)diag.bus_locked,
                 (unsigned)diag.bus_recoveries,
                 (unsigned)diag.bus_recovery_failures);
    }
}

static void ev_hil_log_irq_diag(const char *stage)
{
    ev_esp8266_irq_diag_snapshot_t diag = {0};

    if (ev_esp8266_irq_get_diag(&diag) == EV_OK) {
        ESP_LOGI(EV_HIL_I2C_TAG,
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

static void ev_hil_test_rtc_reads(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "rtc-read-1000";
    uint32_t i;
    uint8_t rtc_regs[7] = {0};
    const uint32_t before_heap = ev_hil_free_heap();

    for (i = 0U; i < cfg->rtc_read_iterations; ++i) {
        const ev_i2c_status_t status = ev_hil_i2c_read_regs(cfg, cfg->rtc_addr_7bit, 0x00U, rtc_regs, sizeof(rtc_regs));
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            ev_hil_heap_gate(result, name, before_heap);
            return;
        }
    }

    ev_hil_pass(result, name);
    ev_hil_heap_gate(result, name, before_heap);
}

static void ev_hil_test_mcp_read_write(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "mcp23008-read-write-1000";
    uint8_t original_iodir = 0xFFU;
    uint8_t original_olat = 0U;
    uint8_t safe_iodir = 0xFFU;
    uint32_t i;
    const uint32_t before_heap = ev_hil_free_heap();
    ev_i2c_status_t status;

    status = ev_hil_i2c_read_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_IODIR, &original_iodir, 1U);
    if (!ev_hil_i2c_expect_ok(result, name, status, 0U)) {
        ev_hil_heap_gate(result, name, before_heap);
        return;
    }

    status = ev_hil_i2c_read_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_OLAT, &original_olat, 1U);
    if (!ev_hil_i2c_expect_ok(result, name, status, 0U)) {
        ev_hil_heap_gate(result, name, before_heap);
        return;
    }

    /* Keep all expander pins as inputs while exercising OLAT.
     * This verifies write/read I2C traffic without driving unknown external loads.
     */
    status = ev_hil_i2c_write_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_IODIR, &safe_iodir, 1U);
    if (!ev_hil_i2c_expect_ok(result, name, status, 0U)) {
        ev_hil_heap_gate(result, name, before_heap);
        return;
    }

    for (i = 0U; i < cfg->mcp_rw_iterations; ++i) {
        const uint8_t expected = (uint8_t)((i ^ (i >> 3U) ^ 0xA5U) & 0xFFU);
        uint8_t observed = 0U;

        status = ev_hil_i2c_write_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_OLAT, &expected, 1U);
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            break;
        }

        status = ev_hil_i2c_read_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_OLAT, &observed, 1U);
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            break;
        }

        if (observed != expected) {
            ESP_LOGE(EV_HIL_I2C_TAG,
                     "%s iteration=%u expected=0x%02X observed=0x%02X",
                     name,
                     (unsigned)i,
                     (unsigned)expected,
                     (unsigned)observed);
            ev_hil_fail(result, name, "MCP23008 OLAT read-back mismatch");
            break;
        }
    }

    status = ev_hil_i2c_write_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_OLAT, &original_olat, 1U);
    if (!ev_hil_i2c_expect_ok(result, name, status, cfg->mcp_rw_iterations)) {
        ev_hil_heap_gate(result, name, before_heap);
        return;
    }

    status = ev_hil_i2c_write_regs(cfg, cfg->mcp23008_addr_7bit, EV_HIL_MCP23008_REG_IODIR, &original_iodir, 1U);
    if (!ev_hil_i2c_expect_ok(result, name, status, cfg->mcp_rw_iterations)) {
        ev_hil_heap_gate(result, name, before_heap);
        return;
    }

    if (i == cfg->mcp_rw_iterations) {
        ev_hil_pass(result, name);
    }
    ev_hil_heap_gate(result, name, before_heap);
}

static ev_i2c_status_t ev_hil_oled_send_commands(const ev_esp8266_i2c_hil_config_t *cfg,
                                                  const uint8_t *commands,
                                                  size_t command_count)
{
    uint8_t tx[1U + EV_HIL_OLED_DATA_CHUNK_BYTES];
    size_t offset = 0U;

    if ((commands == NULL) && (command_count > 0U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    tx[0] = EV_HIL_OLED_CONTROL_CMD;
    while (offset < command_count) {
        size_t chunk = command_count - offset;
        ev_i2c_status_t status;

        if (chunk > EV_HIL_OLED_DATA_CHUNK_BYTES) {
            chunk = EV_HIL_OLED_DATA_CHUNK_BYTES;
        }

        memcpy(&tx[1], &commands[offset], chunk);
        status = ev_hil_i2c_write_stream(cfg, cfg->oled_addr_7bit, tx, chunk + 1U);
        if (status != EV_I2C_OK) {
            return status;
        }
        offset += chunk;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_hil_oled_send_data(const ev_esp8266_i2c_hil_config_t *cfg,
                                              const uint8_t *data,
                                              size_t data_len)
{
    uint8_t tx[1U + EV_HIL_OLED_DATA_CHUNK_BYTES];
    size_t offset = 0U;

    if ((data == NULL) && (data_len > 0U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    tx[0] = EV_HIL_OLED_CONTROL_DATA;
    while (offset < data_len) {
        size_t chunk = data_len - offset;
        ev_i2c_status_t status;

        if (chunk > EV_HIL_OLED_DATA_CHUNK_BYTES) {
            chunk = EV_HIL_OLED_DATA_CHUNK_BYTES;
        }

        memcpy(&tx[1], &data[offset], chunk);
        status = ev_hil_i2c_write_stream(cfg, cfg->oled_addr_7bit, tx, chunk + 1U);
        if (status != EV_I2C_OK) {
            return status;
        }
        offset += chunk;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_hil_oled_set_window(const ev_esp8266_i2c_hil_config_t *cfg,
                                               uint8_t page,
                                               uint8_t start_col,
                                               uint8_t end_col_exclusive)
{
    const uint8_t commands[] = {
        0x21U,
        start_col,
        (uint8_t)(end_col_exclusive - 1U),
        0x22U,
        page,
        page
    };

    return ev_hil_oled_send_commands(cfg, commands, sizeof(commands));
}

static void ev_hil_test_oled_partial_flush(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "oled-partial-flush";
    uint8_t pattern[16];
    uint32_t i;
    uint8_t j;
    const uint32_t before_heap = ev_hil_free_heap();

    for (j = 0U; j < sizeof(pattern); ++j) {
        pattern[j] = (uint8_t)((j & 1U) ? 0x55U : 0xAAU);
    }

    for (i = 0U; i < cfg->oled_partial_flushes; ++i) {
        const uint8_t page = (uint8_t)(i % EV_HIL_OLED_PAGE_COUNT);
        ev_i2c_status_t status = ev_hil_oled_set_window(cfg, page, 0U, (uint8_t)sizeof(pattern));
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            ev_hil_heap_gate(result, name, before_heap);
            return;
        }
        status = ev_hil_oled_send_data(cfg, pattern, sizeof(pattern));
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            ev_hil_heap_gate(result, name, before_heap);
            return;
        }
    }

    ev_hil_pass(result, name);
    ev_hil_heap_gate(result, name, before_heap);
}

static void ev_hil_test_oled_full_flush(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "oled-full-scene-flush";
    uint8_t chunk[EV_HIL_OLED_DATA_CHUNK_BYTES];
    uint32_t flush_index;
    const uint32_t before_heap = ev_hil_free_heap();

    for (flush_index = 0U; flush_index < cfg->oled_full_flushes; ++flush_index) {
        uint8_t page;

        for (page = 0U; page < EV_HIL_OLED_PAGE_COUNT; ++page) {
            uint8_t column = 0U;
            ev_i2c_status_t status = ev_hil_oled_set_window(cfg, page, 0U, EV_HIL_OLED_WIDTH);
            if (!ev_hil_i2c_expect_ok(result, name, status, flush_index)) {
                ev_hil_heap_gate(result, name, before_heap);
                return;
            }

            while (column < EV_HIL_OLED_WIDTH) {
                size_t chunk_len = EV_HIL_OLED_WIDTH - column;
                size_t i;

                if (chunk_len > sizeof(chunk)) {
                    chunk_len = sizeof(chunk);
                }
                for (i = 0U; i < chunk_len; ++i) {
                    chunk[i] = (uint8_t)((flush_index + page + column + i) & 0xFFU);
                }

                status = ev_hil_oled_send_data(cfg, chunk, chunk_len);
                if (!ev_hil_i2c_expect_ok(result, name, status, flush_index)) {
                    ev_hil_heap_gate(result, name, before_heap);
                    return;
                }
                column = (uint8_t)(column + (uint8_t)chunk_len);
            }
        }
    }

    ev_hil_pass(result, name);
    ev_hil_heap_gate(result, name, before_heap);
}

static void ev_hil_test_missing_device_nack(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "missing-device-nack";
    const uint32_t before_heap = ev_hil_free_heap();
    const ev_i2c_status_t status = ev_hil_i2c_write_stream(cfg, cfg->missing_addr_7bit, NULL, 0U);

    if (status == EV_I2C_ERR_NACK) {
        ev_hil_pass(result, name);
    } else {
        ESP_LOGE(EV_HIL_I2C_TAG, "%s expected=NACK actual=%s", name, ev_hil_status_name(status));
        ev_hil_fail(result, name, "missing device did not produce NACK");
    }
    ev_hil_heap_gate(result, name, before_heap);
}

static void ev_hil_test_sda_stuck_low(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "sda-stuck-low-containment";
    ev_i2c_status_t stuck_status;
    ev_i2c_status_t recovery_status;
    const uint32_t before_heap = ev_hil_free_heap();

    if (!ev_hil_fault_gpio_prepare(result, name, cfg->sda_fault_gpio)) {
        return;
    }

    ev_hil_log_fault_fixture_levels(name, "before_fault", cfg->sda_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    ev_hil_fault_gpio_drive_low(cfg->sda_fault_gpio);
    ets_delay_us(20U);
    ev_hil_log_fault_fixture_levels(name, "during_fault", cfg->sda_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    stuck_status = ev_hil_i2c_write_stream(cfg, cfg->rtc_addr_7bit, NULL, 0U);
    ev_hil_fault_gpio_release(cfg->sda_fault_gpio);
    ets_delay_us(20U);
    ev_hil_log_fault_fixture_levels(name, "after_release", cfg->sda_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    recovery_status = ev_hil_i2c_write_stream(cfg, cfg->rtc_addr_7bit, NULL, 0U);

    if (((stuck_status == EV_I2C_ERR_BUS_LOCKED) || (stuck_status == EV_I2C_ERR_TIMEOUT)) &&
        (recovery_status == EV_I2C_OK)) {
        ev_hil_pass(result, name);
    } else {
        ESP_LOGE(EV_HIL_I2C_TAG,
                 "%s sda_fault_gpio=%d sda_bus_gpio=%d scl_bus_gpio=%d stuck_status=%s recovery_status=%s",
                 name,
                 cfg->sda_fault_gpio,
                 EV_BOARD_I2C_SDA_GPIO,
                 EV_BOARD_I2C_SCL_GPIO,
                 ev_hil_status_name(stuck_status),
                 ev_hil_status_name(recovery_status));
        ev_hil_fail(result, name, ev_hil_fault_reason_for_sda(stuck_status, recovery_status));
    }
    ev_hil_heap_gate(result, name, before_heap);
}

static void ev_hil_test_scl_held_low_timeout(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "scl-held-low-timeout";
    ev_i2c_status_t stuck_status;
    ev_i2c_status_t recovery_status;
    const uint32_t before_heap = ev_hil_free_heap();

    if (!ev_hil_fault_gpio_prepare(result, name, cfg->scl_fault_gpio)) {
        return;
    }

    ev_hil_log_fault_fixture_levels(name, "before_fault", cfg->scl_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    ev_hil_fault_gpio_drive_low(cfg->scl_fault_gpio);
    ets_delay_us(20U);
    ev_hil_log_fault_fixture_levels(name, "during_fault", cfg->scl_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    stuck_status = ev_hil_i2c_write_stream(cfg, cfg->rtc_addr_7bit, NULL, 0U);
    ev_hil_fault_gpio_release(cfg->scl_fault_gpio);
    ets_delay_us(20U);
    ev_hil_log_fault_fixture_levels(name, "after_release", cfg->scl_fault_gpio, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    recovery_status = ev_hil_i2c_write_stream(cfg, cfg->rtc_addr_7bit, NULL, 0U);

    if ((stuck_status == EV_I2C_ERR_TIMEOUT) && (recovery_status == EV_I2C_OK)) {
        ev_hil_pass(result, name);
    } else {
        ESP_LOGE(EV_HIL_I2C_TAG,
                 "%s scl_fault_gpio=%d sda_bus_gpio=%d scl_bus_gpio=%d stuck_status=%s recovery_status=%s",
                 name,
                 cfg->scl_fault_gpio,
                 EV_BOARD_I2C_SDA_GPIO,
                 EV_BOARD_I2C_SCL_GPIO,
                 ev_hil_status_name(stuck_status),
                 ev_hil_status_name(recovery_status));
        ev_hil_fail(result, name, ev_hil_fault_reason_for_scl(stuck_status, recovery_status));
    }
    ev_hil_heap_gate(result, name, before_heap);
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

    memset(&s_ev_hil_irq_flood_ctx, 0, sizeof(s_ev_hil_irq_flood_ctx));
    s_ev_hil_irq_flood_ctx.gpio = gpio;
    s_ev_hil_irq_flood_ctx.run = true;

    task = xTaskCreateStatic(ev_hil_irq_flood_task,
                             "ev_hil_irq_flood",
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

    memset(&s_ev_hil_irq_flood_ctx, 0, sizeof(s_ev_hil_irq_flood_ctx));
    s_ev_hil_irq_flood_ctx.gpio = gpio;
    s_ev_hil_irq_flood_ctx.run = true;

    /*
     * MISRA EXCEPTION: the ESP8266 RTOS SDK used by this HIL target may not
     * expose configSUPPORT_STATIC_ALLOCATION. Dynamic task creation is
     * permitted only during HIL bootstrap to start the auxiliary IRQ flood
     * task; the I2C runtime under test remains zero-heap.
     */
    task_rc = xTaskCreate(ev_hil_irq_flood_task,
                          "ev_hil_irq_flood",
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

static void ev_hil_test_irq_flood_during_i2c(const ev_esp8266_i2c_hil_config_t *cfg, ev_hil_suite_result_t *result)
{
    const char *const name = "gpio-irq-flood-during-i2c";
    ev_esp8266_irq_diag_snapshot_t before_irq = {0};
    ev_esp8266_irq_diag_snapshot_t after_irq = {0};
    uint32_t i;
    uint32_t drained = 0U;
    uint8_t rtc_regs[7] = {0};
    uint32_t before_heap = 0U;

    if ((cfg->irq_port == NULL) || (cfg->irq_port->enable == NULL) || (cfg->irq_port->pop == NULL)) {
        ev_hil_skip(result, name, "IRQ port unavailable");
        return;
    }
    if (!ev_hil_gpio_is_valid(cfg->irq_flood_output_gpio)) {
        ev_hil_skip(result, name, "IRQ flood fixture GPIO disabled");
        return;
    }
    (void)cfg->irq_port->enable(cfg->irq_port->ctx, cfg->irq_flood_line_id, true);
    (void)ev_hil_irq_drain(cfg->irq_port);
    (void)ev_esp8266_irq_get_diag(&before_irq);

    if (!ev_hil_irq_flood_start(cfg->irq_flood_output_gpio)) {
        ev_hil_fail(result, name, "failed to start IRQ flood task");
        return;
    }

    before_heap = ev_hil_free_heap();
    for (i = 0U; i < cfg->irq_flood_i2c_transactions; ++i) {
        const ev_i2c_status_t status = ev_hil_i2c_read_regs(cfg, cfg->rtc_addr_7bit, 0x00U, rtc_regs, sizeof(rtc_regs));
        if (!ev_hil_i2c_expect_ok(result, name, status, i)) {
            ev_hil_irq_flood_stop();
            ev_hil_heap_gate(result, name, before_heap);
            return;
        }
        drained += ev_hil_irq_drain(cfg->irq_port);
    }

    ev_hil_irq_flood_stop();
    drained += ev_hil_irq_drain(cfg->irq_port);
    (void)ev_esp8266_irq_get_diag(&after_irq);

    if (after_irq.dropped_samples != before_irq.dropped_samples) {
        ESP_LOGE(EV_HIL_I2C_TAG,
                 "%s dropped_before=%u dropped_after=%u drained=%u toggles=%u",
                 name,
                 (unsigned)before_irq.dropped_samples,
                 (unsigned)after_irq.dropped_samples,
                 (unsigned)drained,
                 (unsigned)s_ev_hil_irq_flood_ctx.toggles);
        ev_hil_fail(result, name, "IRQ samples dropped during I2C flood test");
    } else if (drained == 0U) {
        ev_hil_fail(result, name, "no IRQ samples observed; check flood fixture wiring");
    } else {
        ESP_LOGI(EV_HIL_I2C_TAG,
                 "%s drained=%u toggles=%u dropped_delta=0",
                 name,
                 (unsigned)drained,
                 (unsigned)s_ev_hil_irq_flood_ctx.toggles);
        ev_hil_pass(result, name);
    }
    ev_hil_heap_gate(result, name, before_heap);
}

ev_result_t ev_esp8266_i2c_zero_heap_hil_run(const ev_esp8266_i2c_hil_config_t *cfg)
{
    ev_hil_suite_result_t result = {0};

    if ((cfg == NULL) || !ev_hil_i2c_port_is_valid(cfg->i2c_port)) {
        return EV_ERR_INVALID_ARG;
    }

    ESP_LOGI(EV_HIL_I2C_TAG,
             "HIL start suite=%s board=%s rtc=0x%02X mcp=0x%02X oled=0x%02X missing=0x%02X",
             (cfg->suite_name != NULL) ? cfg->suite_name : "i2c-zero-heap",
             (cfg->board_tag != NULL) ? cfg->board_tag : "unknown",
             (unsigned)cfg->rtc_addr_7bit,
             (unsigned)cfg->mcp23008_addr_7bit,
             (unsigned)cfg->oled_addr_7bit,
             (unsigned)cfg->missing_addr_7bit);
    ev_hil_log_i2c_diag("before", cfg->i2c_port_num);
    ev_hil_log_irq_diag("before");

    ev_hil_test_rtc_reads(cfg, &result);
    ev_hil_test_mcp_read_write(cfg, &result);
    ev_hil_test_oled_partial_flush(cfg, &result);
    ev_hil_test_oled_full_flush(cfg, &result);
    ev_hil_test_missing_device_nack(cfg, &result);
    ev_hil_test_sda_stuck_low(cfg, &result);
    ev_hil_test_scl_held_low_timeout(cfg, &result);
    ev_hil_test_irq_flood_during_i2c(cfg, &result);

    ev_hil_log_i2c_diag("after", cfg->i2c_port_num);
    ev_hil_log_irq_diag("after");
    ESP_LOGI(EV_HIL_I2C_TAG,
             "HIL summary passed=%u failed=%u skipped=%u",
             (unsigned)result.passed,
             (unsigned)result.failed,
             (unsigned)result.skipped);

    if ((result.failed == 0U) && (result.skipped == 0U)) {
        ESP_LOGI(EV_HIL_I2C_TAG, "EV_HIL_RESULT PASS failures=0 skipped=0");
        return EV_OK;
    }

    ESP_LOGE(EV_HIL_I2C_TAG,
             "EV_HIL_RESULT FAIL failures=%u skipped=%u",
             (unsigned)result.failed,
             (unsigned)result.skipped);
    return EV_ERR_STATE;
}
