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

static void seed_default_hw(fake_i2c_port_t *fake_i2c, fake_onewire_port_t *fake_onewire)
{
    uint8_t rtc_time[7];
    uint8_t mcp_gpio = 0xFFU;
    uint8_t scratchpad[9] = {
        0x90U, 0x01U, 0x4BU, 0x46U, 0x7FU, 0xFFU, 0x0CU, 0x10U, 0x00U,
    };

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

    scratchpad[8] = test_ds18b20_crc8(scratchpad, 8U);
    fake_onewire_port_seed_read_bytes(fake_onewire, scratchpad, sizeof(scratchpad));
}

static void bind_default_ports(ev_clock_port_t *clock_port,
                               ev_log_port_t *log_port,
                               ev_i2c_port_t *i2c_port,
                               ev_irq_port_t *irq_port,
                               ev_onewire_port_t *onewire_port,
                               fake_clock_t *clock,
                               fake_log_t *log,
                               fake_i2c_port_t *fake_i2c,
                               fake_irq_port_t *fake_irq,
                               fake_onewire_port_t *fake_onewire)
{
    memset(clock, 0, sizeof(*clock));
    clock->auto_increment_us = 1000ULL;
    clock_port->ctx = clock;
    clock_port->mono_now_us = fake_mono_now_us;
    clock_port->wall_now_us = fake_wall_now_us;
    clock_port->delay_ms = fake_delay_ms;

    memset(log, 0, sizeof(*log));
    log_port->ctx = log;
    log_port->write = fake_log_write;
    log_port->flush = fake_log_flush;

    fake_i2c_port_init(fake_i2c);
    fake_i2c_port_bind(i2c_port, fake_i2c);

    fake_irq_port_init(fake_irq);
    fake_irq_port_bind(irq_port, fake_irq);

    fake_onewire_port_init(fake_onewire);
    fake_onewire_port_bind(onewire_port, fake_onewire);

    seed_default_hw(fake_i2c, fake_onewire);
}

static ev_demo_app_config_t make_default_cfg(ev_clock_port_t *clock_port,
                                             ev_log_port_t *log_port,
                                             ev_i2c_port_t *i2c_port,
                                             ev_irq_port_t *irq_port,
                                             ev_onewire_port_t *onewire_port)
{
    ev_demo_app_config_t cfg = {
        .app_tag = "host_demo",
        .board_name = "host",
        .tick_period_ms = 1000U,
        .clock_port = clock_port,
        .log_port = log_port,
        .irq_port = irq_port,
        .i2c_port = i2c_port,
        .onewire_port = onewire_port,
        .board_profile = &k_fake_full_board_profile,
    };

    return cfg;
}

static void drive_app_until_settled(ev_demo_app_t *app, fake_irq_port_t *fake_irq, unsigned max_iterations)
{
    unsigned i;

    assert(app != NULL);
    assert(fake_irq != NULL);
    assert(max_iterations > 0U);

    for (i = 0U; i < max_iterations; ++i) {
        ev_result_t rc = ev_demo_app_poll(app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
        if ((rc == EV_OK) && (ev_demo_app_pending(app) == 0U) && (fake_irq->count == 0U)) {
            return;
        }
    }

    assert(!"demo app did not settle within iteration budget");
}

static void test_oled_timeout(void)
{
    fake_clock_t clock;
    fake_log_t log;
    fake_i2c_port_t fake_i2c;
    fake_irq_port_t fake_irq;
    fake_onewire_port_t fake_onewire;
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_i2c_port_t i2c_port;
    ev_irq_port_t irq_port;
    ev_onewire_port_t onewire_port;
    ev_demo_app_t app;
    ev_demo_app_config_t cfg;
    const ev_demo_app_stats_t *stats;

    memset(&clock_port, 0, sizeof(clock_port));
    memset(&log_port, 0, sizeof(log_port));
    memset(&i2c_port, 0, sizeof(i2c_port));
    memset(&irq_port, 0, sizeof(irq_port));
    memset(&onewire_port, 0, sizeof(onewire_port));
    bind_default_ports(&clock_port,
                       &log_port,
                       &i2c_port,
                       &irq_port,
                       &onewire_port,
                       &clock,
                       &log,
                       &fake_i2c,
                       &fake_irq,
                       &fake_onewire);
    fake_i2c_port_set_status(&fake_i2c, EV_OLED_DEFAULT_ADDR_7BIT, EV_I2C_ERR_TIMEOUT);
    cfg = make_default_cfg(&clock_port, &log_port, &i2c_port, &irq_port, &onewire_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drive_app_until_settled(&app, &fake_irq, 8U);

    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    assert(app.app_actor.system_ready);
    assert((app.app_actor.active_hardware_mask & EV_SUPERVISOR_HW_OLED) == 0U);
    assert(app.oled_ctx.state == EV_OLED_STATE_ERROR);
    assert(stats->pump_errors == 0U);

    clock.now_us = 1000ULL * 1000ULL;
    drive_app_until_settled(&app, &fake_irq, 8U);
    assert(app.oled_ctx.state == EV_OLED_STATE_ERROR || app.oled_ctx.state == EV_OLED_STATE_READY);
}

static void test_mcp_nack(void)
{
    fake_clock_t clock;
    fake_log_t log;
    fake_i2c_port_t fake_i2c;
    fake_irq_port_t fake_irq;
    fake_onewire_port_t fake_onewire;
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_i2c_port_t i2c_port;
    ev_irq_port_t irq_port;
    ev_onewire_port_t onewire_port;
    ev_demo_app_t app;
    ev_demo_app_config_t cfg;
    const ev_demo_app_stats_t *stats;

    memset(&clock_port, 0, sizeof(clock_port));
    memset(&log_port, 0, sizeof(log_port));
    memset(&i2c_port, 0, sizeof(i2c_port));
    memset(&irq_port, 0, sizeof(irq_port));
    memset(&onewire_port, 0, sizeof(onewire_port));
    bind_default_ports(&clock_port,
                       &log_port,
                       &i2c_port,
                       &irq_port,
                       &onewire_port,
                       &clock,
                       &log,
                       &fake_i2c,
                       &fake_irq,
                       &fake_onewire);
    fake_i2c_port_set_status(&fake_i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, EV_I2C_ERR_NACK);
    cfg = make_default_cfg(&clock_port, &log_port, &i2c_port, &irq_port, &onewire_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drive_app_until_settled(&app, &fake_irq, 8U);

    clock.now_us = 1000ULL * 1000ULL;
    drive_app_until_settled(&app, &fake_irq, 8U);

    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    assert(app.app_actor.system_ready);
    assert((app.app_actor.active_hardware_mask & EV_SUPERVISOR_HW_MCP23008) == 0U);
    assert(app.mcp23008_ctx.configured == false);
    assert(stats->pump_errors == 0U);
}

static void test_rtc_timeout(void)
{
    fake_clock_t clock;
    fake_log_t log;
    fake_i2c_port_t fake_i2c;
    fake_irq_port_t fake_irq;
    fake_onewire_port_t fake_onewire;
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_i2c_port_t i2c_port;
    ev_irq_port_t irq_port;
    ev_onewire_port_t onewire_port;
    ev_demo_app_t app;
    ev_demo_app_config_t cfg;
    const ev_demo_app_stats_t *stats;
    ev_irq_sample_t irq_sample = {
        .line_id = 0U,
        .edge = EV_IRQ_EDGE_FALLING,
        .level = 0U,
        .timestamp_us = 1234U,
    };

    memset(&clock_port, 0, sizeof(clock_port));
    memset(&log_port, 0, sizeof(log_port));
    memset(&i2c_port, 0, sizeof(i2c_port));
    memset(&irq_port, 0, sizeof(irq_port));
    memset(&onewire_port, 0, sizeof(onewire_port));
    bind_default_ports(&clock_port,
                       &log_port,
                       &i2c_port,
                       &irq_port,
                       &onewire_port,
                       &clock,
                       &log,
                       &fake_i2c,
                       &fake_irq,
                       &fake_onewire);
    cfg = make_default_cfg(&clock_port, &log_port, &i2c_port, &irq_port, &onewire_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drive_app_until_settled(&app, &fake_irq, 8U);
    assert(app.app_actor.time_valid == true);

    fake_i2c_port_set_status(&fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, EV_I2C_ERR_TIMEOUT);
    assert(fake_irq_port_push(&fake_irq, &irq_sample) == EV_OK);
    drive_app_until_settled(&app, &fake_irq, 8U);

    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    assert(app.app_actor.time_valid == true);
    assert(app.rtc_ctx.read_failures >= 1U);
    assert(stats->pump_errors == 0U);
}

int main(void)
{
    test_oled_timeout();
    test_mcp_nack();
    test_rtc_timeout();
    return 0;
}
