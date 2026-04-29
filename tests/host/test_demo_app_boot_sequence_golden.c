#include <assert.h>
#include <string.h>

#include "ev/demo_app.h"
#include "fakes/fake_log_port.h"

typedef struct { ev_time_mono_us_t now_us; } fake_clock_t;

static ev_result_t fake_mono_now_us(void *ctx, ev_time_mono_us_t *out_now)
{
    fake_clock_t *clock = (fake_clock_t *)ctx;
    assert(clock != 0);
    assert(out_now != 0);
    *out_now = clock->now_us;
    return EV_OK;
}

static ev_result_t fake_wall_now_us(void *ctx, ev_time_wall_us_t *out_now)
{
    (void)ctx; (void)out_now; return EV_ERR_UNSUPPORTED;
}

static ev_result_t fake_delay_ms(void *ctx, uint32_t delay_ms)
{
    fake_clock_t *clock = (fake_clock_t *)ctx;
    assert(clock != 0);
    clock->now_us += ((ev_time_mono_us_t)delay_ms * 1000ULL);
    return EV_OK;
}

int main(void)
{
    fake_clock_t clock = {0};
    ev_clock_port_t clock_port = {&clock, fake_mono_now_us, fake_wall_now_us, fake_delay_ms};
    fake_log_port_t fake_log;
    ev_log_port_t log_port;
    ev_demo_app_t app;
    ev_demo_app_config_t cfg;
    const ev_demo_app_stats_t *stats;

    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "boot_golden";
    cfg.board_name = "host_no_hw";
    cfg.tick_period_ms = 1000U;
    cfg.clock_port = &clock_port;
    cfg.log_port = &log_port;
    cfg.board_profile = ev_demo_app_default_board_profile();

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_pending(&app) == 0U);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    assert(ev_demo_app_pending(&app) > 0U);
    assert(ev_demo_app_poll(&app) == EV_OK);

    stats = ev_demo_app_stats(&app);
    assert(stats != 0);
    assert(stats->boot_completions == 1U);
    assert(stats->publish_errors == 0U);
    assert(stats->pump_errors == 0U);
    assert(stats->snapshots_published >= 1U);
    assert(stats->snapshots_received >= 1U);
    return 0;
}
