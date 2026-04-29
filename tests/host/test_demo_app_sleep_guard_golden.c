#include <assert.h>
#include <string.h>

#include "ev/demo_app.h"
#include "ev/publish.h"
#include "fakes/fake_board_profile.h"
#include "fakes/fake_i2c_port.h"
#include "fakes/fake_irq_port.h"
#include "fakes/fake_log_port.h"
#include "fakes/fake_onewire_port.h"
#include "fakes/fake_system_port.h"

typedef struct { ev_time_mono_us_t now_us; } fake_clock_t;
static ev_result_t fake_mono_now_us(void *ctx, ev_time_mono_us_t *out_now) { fake_clock_t *c=(fake_clock_t*)ctx; assert(c&&out_now); *out_now=c->now_us; return EV_OK; }
static ev_result_t fake_wall_now_us(void *ctx, ev_time_wall_us_t *out_now) { (void)ctx; (void)out_now; return EV_ERR_UNSUPPORTED; }
static ev_result_t fake_delay_ms(void *ctx, uint32_t delay_ms) { fake_clock_t *c=(fake_clock_t*)ctx; assert(c); c->now_us += (ev_time_mono_us_t)delay_ms*1000ULL; return EV_OK; }

static uint8_t bcd(uint8_t v) { return (uint8_t)(((v / 10U) << 4U) | (v % 10U)); }
static void seed_hw(fake_i2c_port_t *i2c)
{
    uint8_t rtc_time[7] = { bcd(1U), bcd(2U), bcd(3U), bcd(4U), bcd(5U), bcd(6U), bcd(25U) };
    uint8_t gpio = 0xFFU;
    fake_i2c_port_set_present(i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(i2c, EV_RTC_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(i2c, EV_OLED_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_seed_regs(i2c, EV_RTC_DEFAULT_ADDR_7BIT, 0U, rtc_time, sizeof(rtc_time));
    fake_i2c_port_seed_regs(i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, 0x09U, &gpio, 1U);
}

int main(void)
{
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port = {&clock, fake_mono_now_us, fake_wall_now_us, fake_delay_ms};
    fake_log_port_t fake_log; ev_log_port_t log_port;
    fake_i2c_port_t fake_i2c; ev_i2c_port_t i2c_port;
    fake_irq_port_t fake_irq; ev_irq_port_t irq_port;
    fake_onewire_port_t fake_onewire; ev_onewire_port_t onewire_port;
    fake_system_port_t fake_system; ev_system_port_t system_port;
    ev_demo_app_t app; ev_demo_app_config_t cfg;

    fake_log_port_init(&fake_log); fake_log_port_bind(&log_port, &fake_log);
    fake_i2c_port_init(&fake_i2c); fake_i2c_port_bind(&i2c_port, &fake_i2c); seed_hw(&fake_i2c);
    fake_irq_port_init(&fake_irq); fake_irq_port_bind(&irq_port, &fake_irq);
    fake_onewire_port_init(&fake_onewire); fake_onewire.present = false; fake_onewire_port_bind(&onewire_port, &fake_onewire);
    fake_system_port_init(&fake_system); fake_system_port_bind(&system_port, &fake_system);
    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "sleep_guard_golden"; cfg.board_name = "host"; cfg.tick_period_ms = 1000U;
    cfg.clock_port = &clock_port; cfg.log_port = &log_port; cfg.irq_port = &irq_port; cfg.i2c_port = &i2c_port;
    cfg.onewire_port = &onewire_port; cfg.system_port = &system_port; cfg.board_profile = &k_fake_full_board_profile;
    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    assert(ev_demo_app_poll(&app) == EV_OK);
    app.oled_ctx.pending_flush = true;
    {
        ev_power_quiescence_report_t report;
        assert(app.power_ctx.quiescence_guard(app.power_ctx.quiescence_guard_ctx, 42000ULL, &report) == EV_ERR_STATE);
        assert(report.pending_oled_flush == 1U);
    }
    app.oled_ctx.pending_flush = false;
    app.ds18b20_ctx.conversion_pending = true;
    {
        ev_power_quiescence_report_t report;
        assert(app.power_ctx.quiescence_guard(app.power_ctx.quiescence_guard_ctx, 42000ULL, &report) == EV_ERR_STATE);
        assert(report.pending_ds18b20_conversion == 1U);
    }
    return 0;
}
