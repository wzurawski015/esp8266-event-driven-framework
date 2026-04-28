#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/demo_app.h"
#include "fakes/fake_i2c_port.h"
#include "fakes/fake_irq_port.h"
#include "fakes/fake_log_port.h"
#include "fakes/fake_onewire_port.h"

#define TEST_CUSTOM_MCP_ADDR 0x21U
#define TEST_CUSTOM_RTC_ADDR 0x69U
#define TEST_CUSTOM_OLED_ADDR 0x3DU
#define TEST_CUSTOM_RTC_IRQ_LINE 3U

#define TEST_MAX_DRAIN_POLLS 16U

typedef struct {
    ev_time_mono_us_t now_us;
} fake_clock_t;

static ev_result_t fake_mono_now_us(void *ctx, ev_time_mono_us_t *out_now)
{
    fake_clock_t *clock = (fake_clock_t *)ctx;

    assert(clock != NULL);
    assert(out_now != NULL);
    *out_now = clock->now_us;
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
    clock->now_us += ((ev_time_mono_us_t)delay_ms * 1000ULL);
    return EV_OK;
}

static void bind_clock(ev_clock_port_t *out_port, fake_clock_t *clock)
{
    memset(out_port, 0, sizeof(*out_port));
    out_port->ctx = clock;
    out_port->mono_now_us = fake_mono_now_us;
    out_port->wall_now_us = fake_wall_now_us;
    out_port->delay_ms = fake_delay_ms;
}

static uint8_t bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4U) | (value % 10U));
}

static void seed_rtc(fake_i2c_port_t *fake, uint8_t addr)
{
    const uint8_t regs[7] = {
        bcd(1U),
        bcd(2U),
        bcd(3U),
        bcd(4U),
        bcd(5U),
        bcd(6U),
        bcd(25U),
    };

    fake_i2c_port_seed_regs(fake, addr, 0U, regs, sizeof(regs));
}

static void drain(ev_demo_app_t *app)
{
    unsigned i;

    for (i = 0U; i < TEST_MAX_DRAIN_POLLS; ++i) {
        ev_result_t rc = ev_demo_app_poll(app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
        if ((rc == EV_OK) && (ev_demo_app_pending(app) == 0U)) {
            return;
        }
    }

    assert(!"demo app did not drain within bounded test budget");
}

static ev_demo_app_config_t make_base_cfg(ev_clock_port_t *clock_port,
                                          ev_log_port_t *log_port,
                                          const ev_demo_app_board_profile_t *profile)
{
    ev_demo_app_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "test";
    cfg.board_name = "bsp-runtime-profile";
    cfg.tick_period_ms = 1000U;
    cfg.clock_port = clock_port;
    cfg.log_port = log_port;
    cfg.board_profile = profile;
    return cfg;
}

static void test_no_hardware_profile_accepts_missing_hardware_ports(void)
{
    static const ev_demo_app_board_profile_t profile = {
        .capabilities_mask = 0U,
        .hardware_present_mask = 0U,
        .supervisor_required_mask = 0U,
        .supervisor_optional_mask = 0U,
        .i2c_port_num = EV_I2C_PORT_NUM_0,
        .rtc_sqw_line_id = 0U,
        .mcp23008_addr_7bit = 0U,
        .rtc_addr_7bit = 0U,
        .oled_addr_7bit = 0U,
        .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    };
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;
    const ev_demo_app_stats_t *stats;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    cfg = make_base_cfg(&clock_port, &log_port, &profile);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drain(&app);

    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    assert(stats->disabled_route_deliveries > 0U);
    assert(app.supervisor_ctx.required_hardware_mask == 0U);
    assert(app.supervisor_ctx.known_hardware_mask == 0U);
}

static void test_board_profile_addresses_drive_hardware_actor_init(void)
{
    static const ev_demo_app_board_profile_t profile = {
        .capabilities_mask = EV_DEMO_APP_BOARD_CAP_I2C0 | EV_DEMO_APP_BOARD_CAP_GPIO_IRQ,
        .hardware_present_mask = EV_SUPERVISOR_HW_MCP23008 |
                                 EV_SUPERVISOR_HW_RTC |
                                 EV_SUPERVISOR_HW_OLED,
        .supervisor_required_mask = EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC,
        .supervisor_optional_mask = EV_SUPERVISOR_HW_OLED,
        .i2c_port_num = EV_I2C_PORT_NUM_0,
        .rtc_sqw_line_id = TEST_CUSTOM_RTC_IRQ_LINE,
        .mcp23008_addr_7bit = TEST_CUSTOM_MCP_ADDR,
        .rtc_addr_7bit = TEST_CUSTOM_RTC_ADDR,
        .oled_addr_7bit = TEST_CUSTOM_OLED_ADDR,
        .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    };
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    fake_i2c_port_t fake_i2c;
    ev_i2c_port_t i2c_port;
    fake_irq_port_t fake_irq;
    ev_irq_port_t irq_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    fake_i2c_port_init(&fake_i2c);
    fake_i2c_port_bind(&i2c_port, &fake_i2c);
    fake_irq_port_init(&fake_irq);
    fake_irq_port_bind(&irq_port, &fake_irq);

    fake_i2c_port_set_present(&fake_i2c, TEST_CUSTOM_MCP_ADDR, true);
    fake_i2c_port_set_present(&fake_i2c, TEST_CUSTOM_RTC_ADDR, true);
    fake_i2c_port_set_present(&fake_i2c, TEST_CUSTOM_OLED_ADDR, true);
    seed_rtc(&fake_i2c, TEST_CUSTOM_RTC_ADDR);

    cfg = make_base_cfg(&clock_port, &log_port, &profile);
    cfg.i2c_port = &i2c_port;
    cfg.irq_port = &irq_port;

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drain(&app);

    assert(fake_i2c.write_regs_calls_by_addr[TEST_CUSTOM_MCP_ADDR] > 0U);
    assert(fake_i2c.read_regs_calls_by_addr[TEST_CUSTOM_RTC_ADDR] > 0U);
    assert(fake_i2c.write_stream_calls_by_addr[TEST_CUSTOM_OLED_ADDR] > 0U);
    assert(fake_i2c.write_regs_calls_by_addr[EV_MCP23008_DEFAULT_ADDR_7BIT] == 0U);
    assert(fake_i2c.read_regs_calls_by_addr[EV_RTC_DEFAULT_ADDR_7BIT] == 0U);
    assert(fake_i2c.write_stream_calls_by_addr[EV_OLED_DEFAULT_ADDR_7BIT] == 0U);
    assert(app.supervisor_ctx.required_hardware_mask == profile.supervisor_required_mask);
    assert(app.supervisor_ctx.optional_hardware_mask == profile.supervisor_optional_mask);
    assert(app.rtc_ctx.sqw_line_id == TEST_CUSTOM_RTC_IRQ_LINE);
}


static void test_hardware_profile_rejects_zero_device_address(void)
{
    static const ev_demo_app_board_profile_t profile = {
        .capabilities_mask = EV_DEMO_APP_BOARD_CAP_I2C0,
        .hardware_present_mask = EV_SUPERVISOR_HW_RTC,
        .supervisor_required_mask = EV_SUPERVISOR_HW_RTC,
        .supervisor_optional_mask = 0U,
        .i2c_port_num = EV_I2C_PORT_NUM_0,
        .rtc_sqw_line_id = TEST_CUSTOM_RTC_IRQ_LINE,
        .mcp23008_addr_7bit = 0U,
        .rtc_addr_7bit = 0U,
        .oled_addr_7bit = 0U,
        .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    };
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    fake_i2c_port_t fake_i2c;
    ev_i2c_port_t i2c_port;
    fake_irq_port_t fake_irq;
    ev_irq_port_t irq_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    fake_i2c_port_init(&fake_i2c);
    fake_i2c_port_bind(&i2c_port, &fake_i2c);
    fake_irq_port_init(&fake_irq);
    fake_irq_port_bind(&irq_port, &fake_irq);
    cfg = make_base_cfg(&clock_port, &log_port, &profile);
    cfg.i2c_port = &i2c_port;
    cfg.irq_port = &irq_port;

    assert(ev_demo_app_init(&app, &cfg) == EV_ERR_INVALID_ARG);
}

static void test_i2c_profile_requires_valid_i2c_port(void)
{
    static const ev_demo_app_board_profile_t profile = {
        .capabilities_mask = EV_DEMO_APP_BOARD_CAP_I2C0,
        .hardware_present_mask = EV_SUPERVISOR_HW_OLED,
        .supervisor_required_mask = 0U,
        .supervisor_optional_mask = EV_SUPERVISOR_HW_OLED,
        .i2c_port_num = EV_I2C_PORT_NUM_0,
        .rtc_sqw_line_id = 0U,
        .mcp23008_addr_7bit = 0U,
        .rtc_addr_7bit = 0U,
        .oled_addr_7bit = TEST_CUSTOM_OLED_ADDR,
        .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    };
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    cfg = make_base_cfg(&clock_port, &log_port, &profile);

    assert(ev_demo_app_init(&app, &cfg) == EV_ERR_INVALID_ARG);
}

int main(void)
{
    test_no_hardware_profile_accepts_missing_hardware_ports();
    test_board_profile_addresses_drive_hardware_actor_init();
    test_hardware_profile_rejects_zero_device_address();
    test_i2c_profile_requires_valid_i2c_port();
    puts("BSP runtime profile tests passed");
    return 0;
}
