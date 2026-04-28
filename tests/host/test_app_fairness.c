#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/demo_app.h"
#include "fakes/fake_board_profile.h"
#include "fakes/fake_i2c_port.h"
#include "fakes/fake_irq_port.h"
#include "fakes/fake_onewire_port.h"

typedef struct {
    ev_time_mono_us_t now_us;
    ev_time_mono_us_t auto_increment_us;
    uint32_t delay_calls;
} fake_clock_t;

typedef struct {
    uint32_t writes;
    char last_message[192];
} fake_log_t;

static ev_result_t fake_mono_now_us(void *ctx, ev_time_mono_us_t *out_now)
{
    fake_clock_t *clock = (fake_clock_t *)ctx;
    assert(clock != NULL);
    assert(out_now != NULL);
    *out_now = clock->now_us;
    clock->now_us += clock->auto_increment_us;
    return EV_OK;
}

static ev_result_t fake_wall_now_us(void *ctx, ev_time_wall_us_t *out_now)
{
    (void)ctx;
    (void)out_now;
    return EV_ERR_UNSUPPORTED;
}

static ev_result_t fake_delay_ms(void *ctx, uint32_t delay_ms)
{
    fake_clock_t *clock = (fake_clock_t *)ctx;
    assert(clock != NULL);
    (void)delay_ms;
    ++clock->delay_calls;
    return EV_OK;
}

static ev_result_t fake_log_write(void *ctx,
                                  ev_log_level_t level,
                                  const char *tag,
                                  const char *message,
                                  size_t message_len)
{
    fake_log_t *log = (fake_log_t *)ctx;
    (void)level;
    (void)tag;
    assert(log != NULL);
    assert(message != NULL);
    assert(message_len < sizeof(log->last_message));
    memcpy(log->last_message, message, message_len);
    log->last_message[message_len] = '\0';
    ++log->writes;
    return EV_OK;
}

static ev_result_t fake_log_flush(void *ctx)
{
    (void)ctx;
    return EV_OK;
}

static uint8_t test_bcd(uint8_t decimal)
{
    return (uint8_t)(((decimal / 10U) << 4U) | (decimal % 10U));
}

static uint8_t test_ds18b20_crc8(const uint8_t *data, size_t data_len)
{
    uint8_t crc = 0U;
    size_t i;

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

static void seed_common_hw(fake_i2c_port_t *fake_i2c,
                           fake_irq_port_t *fake_irq,
                           fake_onewire_port_t *fake_onewire,
                           ev_i2c_port_t *i2c_port,
                           ev_irq_port_t *irq_port,
                           ev_onewire_port_t *onewire_port)
{
    uint8_t rtc_time[7];
    uint8_t mcp_gpio = 0xFFU;
    uint8_t scratchpad[9] = {
        0x90U, 0x01U, 0x4BU, 0x46U, 0x7FU, 0xFFU, 0x0CU, 0x10U, 0x00U,
    };

    fake_i2c_port_init(fake_i2c);
    fake_i2c_port_bind(i2c_port, fake_i2c);
    fake_i2c_port_set_present(fake_i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(fake_i2c, EV_OLED_DEFAULT_ADDR_7BIT, true);

    rtc_time[0] = test_bcd(56U);
    rtc_time[1] = test_bcd(34U);
    rtc_time[2] = test_bcd(12U);
    rtc_time[3] = test_bcd(3U);
    rtc_time[4] = test_bcd(19U);
    rtc_time[5] = test_bcd(3U);
    rtc_time[6] = test_bcd(24U);
    fake_i2c_port_seed_regs(fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, 0x00U, rtc_time, sizeof(rtc_time));
    fake_i2c_port_seed_regs(fake_i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, 0x09U, &mcp_gpio, 1U);

    fake_irq_port_init(fake_irq);
    fake_irq_port_bind(irq_port, fake_irq);

    fake_onewire_port_init(fake_onewire);
    scratchpad[8] = test_ds18b20_crc8(scratchpad, 8U);
    fake_onewire_port_seed_read_bytes(fake_onewire, scratchpad, sizeof(scratchpad));
    fake_onewire_port_bind(onewire_port, fake_onewire);
}

int main(void)
{
    fake_clock_t clock = {0};
    fake_log_t log = {0};
    fake_i2c_port_t fake_i2c;
    fake_irq_port_t fake_irq;
    fake_onewire_port_t fake_onewire;
    ev_clock_port_t clock_port = {
        .ctx = &clock,
        .mono_now_us = fake_mono_now_us,
        .wall_now_us = fake_wall_now_us,
        .delay_ms = fake_delay_ms,
    };
    ev_log_port_t log_port = {
        .ctx = &log,
        .write = fake_log_write,
        .flush = fake_log_flush,
    };
    ev_i2c_port_t i2c_port = {0};
    ev_irq_port_t irq_port = {0};
    ev_onewire_port_t onewire_port = {0};
    ev_demo_app_t app;
    ev_demo_app_config_t cfg = {
        .app_tag = "host_fairness",
        .board_name = "host",
        .tick_period_ms = 1000U,
        .clock_port = &clock_port,
        .log_port = &log_port,
        .irq_port = &irq_port,
        .i2c_port = &i2c_port,
        .onewire_port = &onewire_port,
        .board_profile = &k_fake_full_board_profile,
    };
    ev_irq_sample_t sample = {
        .line_id = 0U,
        .edge = EV_IRQ_EDGE_FALLING,
        .level = 0U,
        .timestamp_us = 1234U,
    };
    const ev_demo_app_stats_t *stats;
    const ev_system_pump_stats_t *pump_stats;
    uint32_t before_irq_samples;
    uint32_t before_run_calls;
    size_t before_messages;
    size_t before_turns;
    size_t i;

    clock.auto_increment_us = 1000ULL;
    seed_common_hw(&fake_i2c, &fake_irq, &fake_onewire, &i2c_port, &irq_port, &onewire_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);

    for (i = 0U; i < 4U; ++i) {
        sample.timestamp_us = 5000U + (uint32_t)i;
        assert(fake_irq_port_push(&fake_irq, &sample) == EV_OK);
    }
    assert(ev_demo_app_pending(&app) > 0U);
    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    before_irq_samples = stats->irq_samples_drained;
    {
        ev_result_t first_rc = ev_demo_app_poll(&app);
        assert((first_rc == EV_OK) || (first_rc == EV_ERR_PARTIAL));
    }
    stats = ev_demo_app_stats(&app);
    assert(stats->irq_samples_drained > before_irq_samples);
    assert(stats->irq_ring_high_watermark_observed >= 4U);

    while ((ev_demo_app_pending(&app) > 0U) || (fake_irq.count > 0U)) {
        ev_result_t rc = ev_demo_app_poll(&app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
    }
    assert(ev_demo_app_pending(&app) == 0U);
    assert(fake_irq.count == 0U);
    assert(app.app_actor.time_valid);

    for (i = 0U; i < 20U; ++i) {
        sample.timestamp_us = 10000U + (uint32_t)i;
        assert(fake_irq_port_push(&fake_irq, &sample) == EV_OK);
    }

    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    pump_stats = ev_demo_app_system_pump_stats(&app);
    assert(pump_stats != NULL);
    before_irq_samples = stats->irq_samples_drained;
    before_run_calls = pump_stats->run_calls;
    before_messages = pump_stats->messages_processed;
    before_turns = pump_stats->turns_processed;

    assert(ev_demo_app_poll(&app) == EV_ERR_PARTIAL);
    assert(app.mcp23008_ctx.configured);
    assert(app.app_actor.time_valid);
    assert(fake_irq.count > 0U || ev_demo_app_pending(&app) > 0U);

    stats = ev_demo_app_stats(&app);
    pump_stats = ev_demo_app_system_pump_stats(&app);
    assert(stats->irq_samples_drained - before_irq_samples <= 16U);
    assert(pump_stats->messages_processed - before_messages <= 32U);
    assert(pump_stats->turns_processed - before_turns <= 10U);
    assert(pump_stats->run_calls - before_run_calls <= 10U);
    assert(stats->pump_errors == 0U);

    while ((ev_demo_app_pending(&app) > 0U) || (fake_irq.count > 0U)) {
        ev_result_t rc = ev_demo_app_poll(&app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
    }

    assert(fake_irq.count == 0U);
    assert(ev_demo_app_pending(&app) == 0U);
    assert(app.app_actor.time_valid);
    return 0;
}
