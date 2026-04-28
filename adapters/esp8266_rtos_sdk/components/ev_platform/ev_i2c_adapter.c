#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "rom/ets_sys.h"

#include "ev/esp8266_port_adapters.h"

/*
 * Zero-heap ESP8266 I2C master.
 *
 * The ESP8266 RTOS SDK command-link API allocates command descriptors from the
 * heap through command-link creation.  This adapter intentionally does not install or
 * use the SDK I2C command-link driver.  The ESP8266 RTOS SDK does not expose
 * FreeRTOS static semaphore allocation in its supported configuration, so this
 * adapter owns one bootstrap-time mutex and a bounded GPIO open-drain software
 * master for all runtime transactions.
 */
#define EV_I2C_MAX_COMMANDS_PER_TRANSACTION 16U

#define EV_ESP8266_I2C_CMD_TIMEOUT_MS 150U
#define EV_ESP8266_I2C_TRANSACTION_TIMEOUT_US ((int64_t)EV_ESP8266_I2C_CMD_TIMEOUT_MS * 1000LL)
#define EV_ESP8266_I2C_MUTEX_TIMEOUT_MS 250U
#define EV_ESP8266_I2C_MUTEX_TIMEOUT_TICKS pdMS_TO_TICKS(EV_ESP8266_I2C_MUTEX_TIMEOUT_MS)
#define EV_ESP8266_I2C_HALF_PERIOD_US 5U
#define EV_ESP8266_I2C_CLOCK_STRETCH_TIMEOUT_US 300U
#define EV_ESP8266_I2C_RECOVERY_PULSES 9U
#define EV_ESP8266_I2C_MAX_PAYLOAD_BYTES 64U

#define EV_ESP8266_I2C_WRITE_STREAM_COMMANDS 4U
#define EV_ESP8266_I2C_WRITE_REGS_COMMANDS 5U
#define EV_ESP8266_I2C_READ_REGS_COMMANDS 8U

#if EV_ESP8266_I2C_WRITE_STREAM_COMMANDS > EV_I2C_MAX_COMMANDS_PER_TRANSACTION
#error "EV_I2C_MAX_COMMANDS_PER_TRANSACTION is too small for write_stream"
#endif
#if EV_ESP8266_I2C_WRITE_REGS_COMMANDS > EV_I2C_MAX_COMMANDS_PER_TRANSACTION
#error "EV_I2C_MAX_COMMANDS_PER_TRANSACTION is too small for write_regs"
#endif
#if EV_ESP8266_I2C_READ_REGS_COMMANDS > EV_I2C_MAX_COMMANDS_PER_TRANSACTION
#error "EV_I2C_MAX_COMMANDS_PER_TRANSACTION is too small for read_regs"
#endif

static const char *const k_ev_i2c_tag = "ev_i2c";

typedef struct ev_esp8266_i2c_adapter_ctx {
    ev_i2c_port_num_t port_num;
    int sda_pin;
    int scl_pin;
    bool configured;
    uint32_t transactions_started;
    uint32_t transactions_failed;
    uint32_t nacks;
    uint32_t timeouts;
    uint32_t bus_locked;
    uint32_t bus_recoveries;
    uint32_t bus_recovery_failures;
    uint32_t sleep_prepare_attempts;
    uint32_t sleep_prepare_failures;
    volatile bool transaction_active;
} ev_esp8266_i2c_adapter_ctx_t;

static SemaphoreHandle_t g_ev_i2c_bus_mutex = NULL;

static ev_esp8266_i2c_adapter_ctx_t g_ev_i2c0_ctx = {
    .port_num = EV_I2C_PORT_NUM_0,
    .sda_pin = -1,
    .scl_pin = -1,
    .configured = false,
};

static bool ev_esp8266_i2c_pin_is_valid(int pin)
{
    /*
     * GPIO16 is intentionally rejected for I2C.  On ESP8266 it is not a normal
     * GPIO matrix pin and is also a common deep-sleep wake line, so using it as
     * open-drain I2C would violate the BSP/power assumptions.
     */
    return (pin >= 0) && (pin <= 15);
}

static bool ev_esp8266_i2c_txn_is_valid(const ev_esp8266_i2c_adapter_ctx_t *ctx,
                                        ev_i2c_port_num_t port_num,
                                        uint8_t device_address_7bit)
{
    return (ctx != NULL) && ctx->configured && (ctx->port_num == port_num) && (device_address_7bit <= 0x7FU);
}

static bool ev_esp8266_i2c_payload_len_is_valid(size_t data_len)
{
    return data_len <= EV_ESP8266_I2C_MAX_PAYLOAD_BYTES;
}

static ev_i2c_status_t ev_esp8266_i2c_record_status(ev_esp8266_i2c_adapter_ctx_t *ctx, ev_i2c_status_t status)
{
    if ((ctx != NULL) && (status != EV_I2C_OK)) {
        ++ctx->transactions_failed;
        switch (status) {
        case EV_I2C_ERR_TIMEOUT:
            ++ctx->timeouts;
            break;
        case EV_I2C_ERR_NACK:
            ++ctx->nacks;
            break;
        case EV_I2C_ERR_BUS_LOCKED:
        default:
            ++ctx->bus_locked;
            break;
        }
    }

    return status;
}

static void ev_esp8266_i2c_release_sda(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    gpio_set_level((gpio_num_t)ctx->sda_pin, 1);
}

static void ev_esp8266_i2c_release_scl(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    gpio_set_level((gpio_num_t)ctx->scl_pin, 1);
}

static void ev_esp8266_i2c_drive_sda_low(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    gpio_set_level((gpio_num_t)ctx->sda_pin, 0);
}

static void ev_esp8266_i2c_drive_scl_low(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    gpio_set_level((gpio_num_t)ctx->scl_pin, 0);
}

static bool ev_esp8266_i2c_sample_sda(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    return gpio_get_level((gpio_num_t)ctx->sda_pin) != 0;
}

static bool ev_esp8266_i2c_sample_scl(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    return gpio_get_level((gpio_num_t)ctx->scl_pin) != 0;
}

static bool ev_esp8266_i2c_deadline_expired(int64_t started_us)
{
    return (esp_timer_get_time() - started_us) >= EV_ESP8266_I2C_TRANSACTION_TIMEOUT_US;
}

static ev_i2c_status_t ev_esp8266_i2c_wait_scl_high(const ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    uint32_t waited_us = 0U;

    while (!ev_esp8266_i2c_sample_scl(ctx)) {
        if (ev_esp8266_i2c_deadline_expired(started_us) ||
            (waited_us >= EV_ESP8266_I2C_CLOCK_STRETCH_TIMEOUT_US)) {
            return EV_I2C_ERR_TIMEOUT;
        }

        ets_delay_us(1U);
        ++waited_us;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_raise_scl(const ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    ev_i2c_status_t status;

    if (ev_esp8266_i2c_deadline_expired(started_us)) {
        return EV_I2C_ERR_TIMEOUT;
    }

    ev_esp8266_i2c_release_scl(ctx);
    status = ev_esp8266_i2c_wait_scl_high(ctx, started_us);
    if (status != EV_I2C_OK) {
        return status;
    }

    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);
    return EV_I2C_OK;
}

static void ev_esp8266_i2c_lower_scl(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    ev_esp8266_i2c_drive_scl_low(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);
}

static void ev_esp8266_i2c_release_bus_lines(const ev_esp8266_i2c_adapter_ctx_t *ctx)
{
    ev_esp8266_i2c_release_sda(ctx);
    ev_esp8266_i2c_release_scl(ctx);
}

static ev_i2c_status_t ev_esp8266_i2c_recover_bus(ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    uint8_t pulse;

    if (ctx == NULL) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    ++ctx->bus_recoveries;

    ev_esp8266_i2c_release_sda(ctx);
    ev_esp8266_i2c_release_scl(ctx);

    for (pulse = 0U; pulse < EV_ESP8266_I2C_RECOVERY_PULSES; ++pulse) {
        ev_i2c_status_t status;

        status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        if (status != EV_I2C_OK) {
            ++ctx->bus_recovery_failures;
            return status;
        }

        if (ev_esp8266_i2c_sample_sda(ctx)) {
            break;
        }

        ev_esp8266_i2c_lower_scl(ctx);
    }

    ev_esp8266_i2c_drive_sda_low(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    {
        ev_i2c_status_t status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        if (status != EV_I2C_OK) {
            ev_esp8266_i2c_release_bus_lines(ctx);
            ++ctx->bus_recovery_failures;
            return status;
        }
    }

    ev_esp8266_i2c_release_sda(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    if (!ev_esp8266_i2c_sample_sda(ctx) || !ev_esp8266_i2c_sample_scl(ctx)) {
        ++ctx->bus_recovery_failures;
        return EV_I2C_ERR_BUS_LOCKED;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_prepare_bus(ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    ev_i2c_status_t status;

    ev_esp8266_i2c_release_bus_lines(ctx);
    status = ev_esp8266_i2c_wait_scl_high(ctx, started_us);
    if (status != EV_I2C_OK) {
        return status;
    }

    if (ev_esp8266_i2c_sample_sda(ctx)) {
        return EV_I2C_OK;
    }

    return ev_esp8266_i2c_recover_bus(ctx, started_us);
}

static ev_i2c_status_t ev_esp8266_i2c_start_condition(ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    ev_i2c_status_t status;

    ev_esp8266_i2c_release_sda(ctx);
    ev_esp8266_i2c_release_scl(ctx);

    status = ev_esp8266_i2c_wait_scl_high(ctx, started_us);
    if (status != EV_I2C_OK) {
        return status;
    }

    if (!ev_esp8266_i2c_sample_sda(ctx)) {
        status = ev_esp8266_i2c_recover_bus(ctx, started_us);
        if (status != EV_I2C_OK) {
            return status;
        }
    }

    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);
    ev_esp8266_i2c_drive_sda_low(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);
    ev_esp8266_i2c_drive_scl_low(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_stop_condition(const ev_esp8266_i2c_adapter_ctx_t *ctx, int64_t started_us)
{
    ev_i2c_status_t status;

    ev_esp8266_i2c_drive_sda_low(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    status = ev_esp8266_i2c_raise_scl(ctx, started_us);
    if (status != EV_I2C_OK) {
        ev_esp8266_i2c_release_sda(ctx);
        return status;
    }

    ev_esp8266_i2c_release_sda(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    if (!ev_esp8266_i2c_sample_sda(ctx)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_write_byte(const ev_esp8266_i2c_adapter_ctx_t *ctx,
                                                  uint8_t value,
                                                  int64_t started_us)
{
    uint8_t bit_index;

    for (bit_index = 0U; bit_index < 8U; ++bit_index) {
        ev_i2c_status_t status;

        if ((value & 0x80U) != 0U) {
            ev_esp8266_i2c_release_sda(ctx);
        } else {
            ev_esp8266_i2c_drive_sda_low(ctx);
        }

        status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        if (status != EV_I2C_OK) {
            ev_esp8266_i2c_release_sda(ctx);
            return status;
        }

        ev_esp8266_i2c_lower_scl(ctx);
        value = (uint8_t)(value << 1U);
    }

    ev_esp8266_i2c_release_sda(ctx);

    {
        ev_i2c_status_t status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        bool acked;

        if (status != EV_I2C_OK) {
            return status;
        }

        acked = !ev_esp8266_i2c_sample_sda(ctx);
        ev_esp8266_i2c_lower_scl(ctx);

        return acked ? EV_I2C_OK : EV_I2C_ERR_NACK;
    }
}

static ev_i2c_status_t ev_esp8266_i2c_read_byte(const ev_esp8266_i2c_adapter_ctx_t *ctx,
                                                 uint8_t *out_value,
                                                 bool ack_after_byte,
                                                 int64_t started_us)
{
    uint8_t value = 0U;
    uint8_t bit_index;

    ev_esp8266_i2c_release_sda(ctx);

    for (bit_index = 0U; bit_index < 8U; ++bit_index) {
        ev_i2c_status_t status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        if (status != EV_I2C_OK) {
            return status;
        }

        value = (uint8_t)(value << 1U);
        if (ev_esp8266_i2c_sample_sda(ctx)) {
            value = (uint8_t)(value | 0x01U);
        }

        ev_esp8266_i2c_lower_scl(ctx);
    }

    if (ack_after_byte) {
        ev_esp8266_i2c_drive_sda_low(ctx);
    } else {
        ev_esp8266_i2c_release_sda(ctx);
    }

    {
        ev_i2c_status_t status = ev_esp8266_i2c_raise_scl(ctx, started_us);
        if (status != EV_I2C_OK) {
            ev_esp8266_i2c_release_sda(ctx);
            return status;
        }
    }

    ev_esp8266_i2c_lower_scl(ctx);
    ev_esp8266_i2c_release_sda(ctx);

    *out_value = value;
    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_write_payload(const ev_esp8266_i2c_adapter_ctx_t *ctx,
                                                     const uint8_t *data,
                                                     size_t data_len,
                                                     int64_t started_us)
{
    size_t offset;

    for (offset = 0U; offset < data_len; ++offset) {
        ev_i2c_status_t status = ev_esp8266_i2c_write_byte(ctx, data[offset], started_us);
        if (status != EV_I2C_OK) {
            return status;
        }
    }

    return EV_I2C_OK;
}

/*
 * Shared-bus timing policy:
 * - transaction timeout must tolerate one bounded OLED page-chunk transfer,
 * - mutex timeout must be longer than one command budget so slower bulk OLED
 *   flushes do not starve MCP23008/RTC access behind a too-aggressive bus lock
 *   window,
 * - runtime transactions never allocate heap memory,
 * - MISRA/Power-of-Ten exception: the ESP8266 RTOS SDK configuration does not
 *   provide supported static semaphore creation.  xSemaphoreCreateMutex() is
 *   therefore permitted here only during boot-time hardware initialization; no
 *   runtime I2C transaction allocates heap memory.
 */
static ev_i2c_status_t ev_esp8266_i2c_take_bus_with_timeout(TickType_t timeout_ticks)
{
    if (g_ev_i2c_bus_mutex == NULL) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    if (xSemaphoreTake(g_ev_i2c_bus_mutex, timeout_ticks) != pdTRUE) {
        return EV_I2C_ERR_TIMEOUT;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t ev_esp8266_i2c_take_bus(void)
{
    return ev_esp8266_i2c_take_bus_with_timeout(EV_ESP8266_I2C_MUTEX_TIMEOUT_TICKS);
}

static void ev_esp8266_i2c_give_bus(void)
{
    if (g_ev_i2c_bus_mutex != NULL) {
        (void)xSemaphoreGive(g_ev_i2c_bus_mutex);
    }
}

static ev_i2c_status_t ev_esp8266_i2c_begin_locked(ev_esp8266_i2c_adapter_ctx_t *adapter, int64_t *out_started_us)
{
    int64_t started_us;
    ev_i2c_status_t status;

    if ((adapter == NULL) || (out_started_us == NULL)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    started_us = esp_timer_get_time();
    *out_started_us = started_us;

    status = ev_esp8266_i2c_prepare_bus(adapter, started_us);
    if (status == EV_I2C_OK) {
        ++adapter->transactions_started;
    }

    return status;
}

static ev_i2c_status_t ev_esp8266_i2c_finish_locked(ev_esp8266_i2c_adapter_ctx_t *adapter,
                                                    bool started,
                                                    int64_t started_us,
                                                    ev_i2c_status_t status)
{
    if (started) {
        const ev_i2c_status_t stop_status = ev_esp8266_i2c_stop_condition(adapter, started_us);
        if (status == EV_I2C_OK) {
            status = stop_status;
        }
    }

    ev_esp8266_i2c_release_bus_lines(adapter);
    return ev_esp8266_i2c_record_status(adapter, status);
}

static ev_i2c_status_t ev_esp8266_i2c_write_stream(void *ctx,
                                                    ev_i2c_port_num_t port_num,
                                                    uint8_t device_address_7bit,
                                                    const uint8_t *data,
                                                    size_t data_len)
{
    ev_esp8266_i2c_adapter_ctx_t *adapter = (ev_esp8266_i2c_adapter_ctx_t *)ctx;
    ev_i2c_status_t status;
    int64_t started_us = 0;
    bool started = false;

    if (!ev_esp8266_i2c_txn_is_valid(adapter, port_num, device_address_7bit)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }
    if (((data_len > 0U) && (data == NULL)) || !ev_esp8266_i2c_payload_len_is_valid(data_len)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    status = ev_esp8266_i2c_take_bus();
    if (status != EV_I2C_OK) {
        return ev_esp8266_i2c_record_status(adapter, status);
    }

    adapter->transaction_active = true;

    status = ev_esp8266_i2c_begin_locked(adapter, &started_us);
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_start_condition(adapter, started_us);
    }
    if (status == EV_I2C_OK) {
        started = true;
        status = ev_esp8266_i2c_write_byte(adapter, (uint8_t)((device_address_7bit << 1U) | 0U), started_us);
    }
    if ((status == EV_I2C_OK) && (data_len > 0U)) {
        status = ev_esp8266_i2c_write_payload(adapter, data, data_len, started_us);
    }

    status = ev_esp8266_i2c_finish_locked(adapter, started, started_us, status);
    adapter->transaction_active = false;
    ev_esp8266_i2c_give_bus();
    return status;
}

static ev_i2c_status_t ev_esp8266_i2c_write_regs(void *ctx,
                                                  ev_i2c_port_num_t port_num,
                                                  uint8_t device_address_7bit,
                                                  uint8_t first_reg,
                                                  const uint8_t *data,
                                                  size_t data_len)
{
    ev_esp8266_i2c_adapter_ctx_t *adapter = (ev_esp8266_i2c_adapter_ctx_t *)ctx;
    ev_i2c_status_t status;
    int64_t started_us = 0;
    bool started = false;

    if (!ev_esp8266_i2c_txn_is_valid(adapter, port_num, device_address_7bit)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }
    if (((data_len > 0U) && (data == NULL)) || !ev_esp8266_i2c_payload_len_is_valid(data_len)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    status = ev_esp8266_i2c_take_bus();
    if (status != EV_I2C_OK) {
        return ev_esp8266_i2c_record_status(adapter, status);
    }

    adapter->transaction_active = true;

    status = ev_esp8266_i2c_begin_locked(adapter, &started_us);
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_start_condition(adapter, started_us);
    }
    if (status == EV_I2C_OK) {
        started = true;
        status = ev_esp8266_i2c_write_byte(adapter, (uint8_t)((device_address_7bit << 1U) | 0U), started_us);
    }
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_write_byte(adapter, first_reg, started_us);
    }
    if ((status == EV_I2C_OK) && (data_len > 0U)) {
        status = ev_esp8266_i2c_write_payload(adapter, data, data_len, started_us);
    }

    status = ev_esp8266_i2c_finish_locked(adapter, started, started_us, status);
    adapter->transaction_active = false;
    ev_esp8266_i2c_give_bus();
    return status;
}

static ev_i2c_status_t ev_esp8266_i2c_read_regs(void *ctx,
                                                 ev_i2c_port_num_t port_num,
                                                 uint8_t device_address_7bit,
                                                 uint8_t first_reg,
                                                 uint8_t *data,
                                                 size_t data_len)
{
    ev_esp8266_i2c_adapter_ctx_t *adapter = (ev_esp8266_i2c_adapter_ctx_t *)ctx;
    ev_i2c_status_t status;
    int64_t started_us = 0;
    size_t offset;
    bool started = false;

    if (!ev_esp8266_i2c_txn_is_valid(adapter, port_num, device_address_7bit)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }
    if ((data == NULL) || (data_len == 0U) || !ev_esp8266_i2c_payload_len_is_valid(data_len)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    status = ev_esp8266_i2c_take_bus();
    if (status != EV_I2C_OK) {
        return ev_esp8266_i2c_record_status(adapter, status);
    }

    adapter->transaction_active = true;

    status = ev_esp8266_i2c_begin_locked(adapter, &started_us);
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_start_condition(adapter, started_us);
    }
    if (status == EV_I2C_OK) {
        started = true;
        status = ev_esp8266_i2c_write_byte(adapter, (uint8_t)((device_address_7bit << 1U) | 0U), started_us);
    }
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_write_byte(adapter, first_reg, started_us);
    }
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_start_condition(adapter, started_us);
    }
    if (status == EV_I2C_OK) {
        status = ev_esp8266_i2c_write_byte(adapter, (uint8_t)((device_address_7bit << 1U) | 1U), started_us);
    }

    for (offset = 0U; (status == EV_I2C_OK) && (offset < data_len); ++offset) {
        const bool ack_after_byte = (offset + 1U) < data_len;
        status = ev_esp8266_i2c_read_byte(adapter, &data[offset], ack_after_byte, started_us);
    }

    status = ev_esp8266_i2c_finish_locked(adapter, started, started_us, status);
    adapter->transaction_active = false;
    ev_esp8266_i2c_give_bus();
    return status;
}

static const char *ev_esp8266_i2c_known_device_name(uint8_t device_address_7bit)
{
    if (device_address_7bit == 0x68U) {
        return "rtc";
    }
    if ((device_address_7bit >= 0x20U) && (device_address_7bit <= 0x27U)) {
        return "mcp23008";
    }
    if ((device_address_7bit == 0x3CU) || (device_address_7bit == 0x3DU)) {
        return "oled";
    }
    return NULL;
}

static ev_esp8266_i2c_adapter_ctx_t *ev_esp8266_i2c_ctx_from_port(ev_i2c_port_num_t port_num)
{
    if (port_num == EV_I2C_PORT_NUM_0) {
        return &g_ev_i2c0_ctx;
    }
    return NULL;
}

ev_result_t ev_i2c_scan(ev_i2c_port_num_t port_num)
{
    ev_esp8266_i2c_adapter_ctx_t *ctx = ev_esp8266_i2c_ctx_from_port(port_num);
    bool found_any = false;
    bool found_rtc = false;
    bool found_oled = false;
    bool found_mcp = false;
    uint8_t device_address_7bit;

    if (ctx == NULL) {
        return EV_ERR_UNSUPPORTED;
    }
    if (!ctx->configured) {
        return EV_ERR_STATE;
    }

    ESP_LOGI(k_ev_i2c_tag,
             "i2c-scan: start port=%u SDA=%d SCL=%d",
             (unsigned)port_num,
             ctx->sda_pin,
             ctx->scl_pin);

    for (device_address_7bit = 0x03U; device_address_7bit < 0x78U; ++device_address_7bit) {
        ev_i2c_status_t status = ev_esp8266_i2c_write_stream(ctx, port_num, device_address_7bit, NULL, 0U);

        if (status == EV_I2C_OK) {
            const char *device_name = ev_esp8266_i2c_known_device_name(device_address_7bit);
            found_any = true;

            if (device_address_7bit == 0x68U) {
                found_rtc = true;
            }
            if ((device_address_7bit >= 0x20U) && (device_address_7bit <= 0x27U)) {
                found_mcp = true;
            }
            if ((device_address_7bit == 0x3CU) || (device_address_7bit == 0x3DU)) {
                found_oled = true;
            }

            if (device_name != NULL) {
                ESP_LOGI(k_ev_i2c_tag,
                         "i2c-scan: found 0x%02X (%s)",
                         (unsigned)device_address_7bit,
                         device_name);
            } else {
                ESP_LOGI(k_ev_i2c_tag, "i2c-scan: found 0x%02X", (unsigned)device_address_7bit);
            }
        }
    }

    if (!found_any) {
        ESP_LOGW(k_ev_i2c_tag, "i2c-scan: no devices acknowledged");
    }
    if (found_rtc) {
        ESP_LOGI(k_ev_i2c_tag, "rtc-probe: detected device at 0x68");
    } else {
        ESP_LOGW(k_ev_i2c_tag, "rtc-probe: no response at 0x68");
    }
    if (found_mcp) {
        ESP_LOGI(k_ev_i2c_tag, "mcp23008-probe: detected device in range 0x20-0x27");
    } else {
        ESP_LOGW(k_ev_i2c_tag, "mcp23008-probe: no response in range 0x20-0x27");
    }
    if (found_oled) {
        ESP_LOGI(k_ev_i2c_tag, "oled-probe: detected device at 0x3C or 0x3D");
    } else {
        ESP_LOGI(k_ev_i2c_tag, "oled-probe: optional OLED not detected");
    }

    return EV_OK;
}


ev_result_t ev_esp8266_i2c_get_diag(ev_i2c_port_num_t port_num, ev_esp8266_i2c_diag_snapshot_t *out_snapshot)
{
    ev_esp8266_i2c_adapter_ctx_t *ctx = ev_esp8266_i2c_ctx_from_port(port_num);

    if (out_snapshot == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if ((ctx == NULL) || !ctx->configured) {
        return EV_ERR_STATE;
    }

    out_snapshot->transactions_started = ctx->transactions_started;
    out_snapshot->transactions_failed = ctx->transactions_failed;
    out_snapshot->nacks = ctx->nacks;
    out_snapshot->timeouts = ctx->timeouts;
    out_snapshot->bus_locked = ctx->bus_locked;
    out_snapshot->bus_recoveries = ctx->bus_recoveries;
    out_snapshot->bus_recovery_failures = ctx->bus_recovery_failures;
    out_snapshot->sleep_prepare_attempts = ctx->sleep_prepare_attempts;
    out_snapshot->sleep_prepare_failures = ctx->sleep_prepare_failures;
    out_snapshot->transaction_active = ctx->transaction_active;
    out_snapshot->sda_high = ev_esp8266_i2c_sample_sda(ctx);
    out_snapshot->scl_high = ev_esp8266_i2c_sample_scl(ctx);
    return EV_OK;
}
ev_result_t ev_esp8266_i2c_prepare_for_sleep(ev_i2c_port_num_t port_num)
{
    ev_esp8266_i2c_adapter_ctx_t *ctx = ev_esp8266_i2c_ctx_from_port(port_num);
    ev_i2c_status_t status;
    int64_t started_us;

    if ((ctx == NULL) || !ctx->configured) {
        return EV_ERR_STATE;
    }

    ++ctx->sleep_prepare_attempts;
    if (ctx->transaction_active) {
        ++ctx->sleep_prepare_failures;
        return EV_ERR_STATE;
    }

    status = ev_esp8266_i2c_take_bus_with_timeout(0);
    if (status != EV_I2C_OK) {
        ++ctx->sleep_prepare_failures;
        (void)ev_esp8266_i2c_record_status(ctx, status);
        return EV_ERR_STATE;
    }

    if (ctx->transaction_active) {
        ++ctx->sleep_prepare_failures;
        ev_esp8266_i2c_give_bus();
        return EV_ERR_STATE;
    }

    started_us = esp_timer_get_time();
    status = ev_esp8266_i2c_prepare_bus(ctx, started_us);
    ev_esp8266_i2c_release_bus_lines(ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);
    if ((status == EV_I2C_OK) && (!ev_esp8266_i2c_sample_sda(ctx) || !ev_esp8266_i2c_sample_scl(ctx))) {
        status = EV_I2C_ERR_BUS_LOCKED;
    }

    ev_esp8266_i2c_give_bus();
    if (status != EV_I2C_OK) {
        ++ctx->sleep_prepare_failures;
        (void)ev_esp8266_i2c_record_status(ctx, status);
        return EV_ERR_STATE;
    }

    return EV_OK;
}

ev_result_t ev_esp8266_i2c_port_init(ev_i2c_port_t *out_port, int sda_pin, int scl_pin)
{
    gpio_config_t sdk_cfg = {0};
    esp_err_t sdk_rc;
    ev_i2c_status_t bus_status;

    if ((out_port == NULL) || !ev_esp8266_i2c_pin_is_valid(sda_pin) || !ev_esp8266_i2c_pin_is_valid(scl_pin) ||
        (sda_pin == scl_pin)) {
        return EV_ERR_INVALID_ARG;
    }

    if (g_ev_i2c_bus_mutex == NULL) {
        /*
         * MISRA EXCEPTION: ESP8266 RTOS SDK lacks supported
         * configSUPPORT_STATIC_ALLOCATION for semaphore creation. Dynamic
         * allocation is permitted here ONLY during the boot-time hardware
         * initialization phase. The runtime I2C hot path remains zero-heap:
         * transactions use the already-created mutex and the bounded GPIO
         * software master, never SDK command links or allocator-backed
         * descriptors.
         */
        g_ev_i2c_bus_mutex = xSemaphoreCreateMutex();
        if (g_ev_i2c_bus_mutex == NULL) {
            ESP_LOGE(k_ev_i2c_tag, "bus mutex allocation failed");
            return EV_ERR_STATE;
        }
    }

    sdk_cfg.pin_bit_mask = (1ULL << (unsigned)sda_pin) | (1ULL << (unsigned)scl_pin);
    sdk_cfg.mode = GPIO_MODE_OUTPUT_OD;
    sdk_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    sdk_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    sdk_cfg.intr_type = GPIO_INTR_DISABLE;

    sdk_rc = gpio_config(&sdk_cfg);
    if (sdk_rc != ESP_OK) {
        ESP_LOGE(k_ev_i2c_tag, "gpio_config for zero-heap i2c failed rc=%d", (int)sdk_rc);
        return EV_ERR_STATE;
    }

    g_ev_i2c0_ctx.port_num = EV_I2C_PORT_NUM_0;
    g_ev_i2c0_ctx.sda_pin = sda_pin;
    g_ev_i2c0_ctx.scl_pin = scl_pin;
    g_ev_i2c0_ctx.configured = true;
    g_ev_i2c0_ctx.transactions_started = 0U;
    g_ev_i2c0_ctx.transactions_failed = 0U;
    g_ev_i2c0_ctx.nacks = 0U;
    g_ev_i2c0_ctx.timeouts = 0U;
    g_ev_i2c0_ctx.bus_locked = 0U;
    g_ev_i2c0_ctx.bus_recoveries = 0U;
    g_ev_i2c0_ctx.bus_recovery_failures = 0U;
    g_ev_i2c0_ctx.sleep_prepare_attempts = 0U;
    g_ev_i2c0_ctx.sleep_prepare_failures = 0U;
    g_ev_i2c0_ctx.transaction_active = false;

    ev_esp8266_i2c_release_bus_lines(&g_ev_i2c0_ctx);
    ets_delay_us(EV_ESP8266_I2C_HALF_PERIOD_US);

    bus_status = ev_esp8266_i2c_prepare_bus(&g_ev_i2c0_ctx, esp_timer_get_time());
    if (bus_status != EV_I2C_OK) {
        ESP_LOGE(k_ev_i2c_tag, "zero-heap i2c bus is not idle after init status=%d", (int)bus_status);
        g_ev_i2c0_ctx.configured = false;
        return EV_ERR_STATE;
    }

    out_port->ctx = &g_ev_i2c0_ctx;
    out_port->write_stream = ev_esp8266_i2c_write_stream;
    out_port->write_regs = ev_esp8266_i2c_write_regs;
    out_port->read_regs = ev_esp8266_i2c_read_regs;

    return EV_OK;
}
