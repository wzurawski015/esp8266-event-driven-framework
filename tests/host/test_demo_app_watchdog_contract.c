#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/actor_runtime.h"
#include "ev/demo_app.h"
#include "ev/route_table.h"
#include "fakes/fake_log_port.h"
#include "fakes/fake_wdt_port.h"

#define TEST_MAX_POLLS 32U

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
    clock->now_us += (ev_time_mono_us_t)delay_ms * 1000ULL;
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

static const ev_demo_app_board_profile_t k_wdt_profile = {
    .capabilities_mask = EV_DEMO_APP_BOARD_CAP_WDT,
    .hardware_present_mask = 0U,
    .supervisor_required_mask = 0U,
    .supervisor_optional_mask = 0U,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = 0U,
    .mcp23008_addr_7bit = 0U,
    .rtc_addr_7bit = 0U,
    .oled_addr_7bit = 0U,
    .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    .watchdog_timeout_ms = 3000U,
};

static void make_base_cfg(ev_demo_app_config_t *cfg,
                          ev_clock_port_t *clock_port,
                          ev_log_port_t *log_port,
                          ev_wdt_port_t *wdt_port)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->app_tag = "test";
    cfg->board_name = "watchdog-profile";
    cfg->tick_period_ms = 1000U;
    cfg->clock_port = clock_port;
    cfg->log_port = log_port;
    cfg->wdt_port = wdt_port;
    cfg->board_profile = &k_wdt_profile;
}

static void drain(ev_demo_app_t *app)
{
    unsigned i;
    for (i = 0U; i < TEST_MAX_POLLS; ++i) {
        ev_result_t rc = ev_demo_app_poll(app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
        if ((rc == EV_OK) && (ev_demo_app_pending(app) == 0U)) {
            return;
        }
    }
    assert(!"demo app did not drain within bounded watchdog test budget");
}

static void test_wdt_profile_requires_port(void)
{
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    make_base_cfg(&cfg, &clock_port, &log_port, NULL);
    assert(ev_demo_app_init(&app, &cfg) == EV_ERR_INVALID_ARG);
}

static void test_watchdog_registered_and_fed_by_tick_route(void)
{
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    fake_wdt_port_t fake_wdt;
    ev_wdt_port_t wdt_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;
    const ev_watchdog_actor_stats_t *wdt_stats;
    unsigned i;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    fake_wdt_port_init(&fake_wdt);
    fake_wdt_port_bind(&wdt_port, &fake_wdt);
    make_base_cfg(&cfg, &clock_port, &log_port, &wdt_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    {
        ev_actor_runtime_t *watchdog_runtime = ev_runtime_graph_get_runtime(&app.graph, ACT_WATCHDOG);
        assert(watchdog_runtime != NULL);
        assert(watchdog_runtime->actor_id == ACT_WATCHDOG);
    }
    assert(fake_wdt.enable_calls == 1U);
    assert(fake_wdt.last_timeout_ms == k_wdt_profile.watchdog_timeout_ms);
    assert(ev_route_exists(EV_TICK_1S, ACT_WATCHDOG));
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drain(&app);

    clock.now_us = 1000ULL * 1000ULL;
    for (i = 0U; (i < TEST_MAX_POLLS) && (fake_wdt.feed_calls == 0U); ++i) {
        ev_result_t rc = ev_demo_app_poll(&app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
    }
    assert(fake_wdt.feed_calls > 0U);
    wdt_stats = ev_demo_app_watchdog_stats(&app);
    assert(wdt_stats != NULL);
    assert(wdt_stats->feeds_ok == fake_wdt.feed_calls);
}

static void test_sleep_arming_prevents_watchdog_feed(void)
{
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port;
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    fake_wdt_port_t fake_wdt;
    ev_wdt_port_t wdt_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;

    bind_clock(&clock_port, &clock);
    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    fake_wdt_port_init(&fake_wdt);
    fake_wdt_port_bind(&wdt_port, &fake_wdt);
    make_base_cfg(&cfg, &clock_port, &log_port, &wdt_port);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    drain(&app);
    app.sleep_arming = true;
    clock.now_us = 1000ULL * 1000ULL;
    assert(ev_demo_app_poll(&app) == EV_OK);
    assert(fake_wdt.feed_calls == 0U);
}

int main(void)
{
    test_wdt_profile_requires_port();
    test_watchdog_registered_and_fed_by_tick_route();
    test_sleep_arming_prevents_watchdog_feed();
    return 0;
}
