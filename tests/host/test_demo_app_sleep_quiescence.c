#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/demo_app.h"
#include "ev/actor_runtime.h"
#include "ev/dispose.h"
#include "ev/publish.h"
#include "ev/runtime_graph.h"
#include "fakes/fake_board_profile.h"
#include "fakes/fake_i2c_port.h"
#include "fakes/fake_irq_port.h"
#include "fakes/fake_onewire_port.h"
#include "fakes/fake_system_port.h"

typedef struct {
    ev_time_mono_us_t now_us;
} fake_clock_t;

typedef struct {
    uint32_t writes;
    uint32_t flushes;
} fake_log_t;

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
    (void)ctx;
    (void)delay_ms;
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
    (void)message;
    (void)message_len;
    assert(log != NULL);
    ++log->writes;
    return EV_OK;
}

static ev_result_t fake_log_flush(void *ctx)
{
    fake_log_t *log = (fake_log_t *)ctx;
    assert(log != NULL);
    ++log->flushes;
    return EV_OK;
}

static uint8_t test_bcd(uint8_t decimal)
{
    return (uint8_t)(((decimal / 10U) << 4U) | (decimal % 10U));
}

typedef struct {
    fake_clock_t clock;
    fake_log_t log;
    fake_i2c_port_t fake_i2c;
    fake_irq_port_t fake_irq;
    fake_onewire_port_t fake_onewire;
    fake_system_port_t fake_system;
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_i2c_port_t i2c_port;
    ev_irq_port_t irq_port;
    ev_onewire_port_t onewire_port;
    ev_system_port_t system_port;
    ev_demo_app_t app;
} sleep_fixture_t;

static void seed_i2c_devices(fake_i2c_port_t *fake_i2c)
{
    uint8_t rtc_time[7];
    uint8_t mcp_gpio = 0xFFU;

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
}

static void sleep_fixture_init(sleep_fixture_t *fx)
{
    ev_demo_app_config_t cfg;

    memset(fx, 0, sizeof(*fx));
    fx->clock.now_us = 0ULL;
    fx->clock_port.ctx = &fx->clock;
    fx->clock_port.mono_now_us = fake_mono_now_us;
    fx->clock_port.wall_now_us = fake_wall_now_us;
    fx->clock_port.delay_ms = fake_delay_ms;
    fx->log_port.ctx = &fx->log;
    fx->log_port.write = fake_log_write;
    fx->log_port.flush = fake_log_flush;

    fake_i2c_port_init(&fx->fake_i2c);
    fake_i2c_port_bind(&fx->i2c_port, &fx->fake_i2c);
    seed_i2c_devices(&fx->fake_i2c);

    fake_irq_port_init(&fx->fake_irq);
    fake_irq_port_bind(&fx->irq_port, &fx->fake_irq);

    fake_onewire_port_init(&fx->fake_onewire);
    fx->fake_onewire.present = false;
    fake_onewire_port_bind(&fx->onewire_port, &fx->fake_onewire);

    fake_system_port_init(&fx->fake_system);
    fake_system_port_bind(&fx->system_port, &fx->fake_system);

    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "sleep_test";
    cfg.board_name = "host";
    cfg.tick_period_ms = 1000U;
    cfg.clock_port = &fx->clock_port;
    cfg.log_port = &fx->log_port;
    cfg.irq_port = &fx->irq_port;
    cfg.i2c_port = &fx->i2c_port;
    cfg.onewire_port = &fx->onewire_port;
    cfg.system_port = &fx->system_port;
    cfg.board_profile = &k_fake_full_board_profile;

    assert(ev_demo_app_init(&fx->app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&fx->app) == EV_OK);
}

static void drive_until_quiescent(sleep_fixture_t *fx)
{
    uint8_t i;

    for (i = 0U; i < 12U; ++i) {
        ev_result_t rc = ev_demo_app_poll(&fx->app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
        if ((ev_demo_app_pending(&fx->app) == 0U) && (fx->fake_irq.count == 0U) &&
            !fx->app.ds18b20_ctx.conversion_pending) {
            return;
        }
    }

    assert(!"sleep fixture failed to quiesce");
}

static ev_result_t publish_sleep(ev_demo_app_t *app, uint32_t duration_ms)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_sys_goto_sleep_cmd_t cmd = {0};
    ev_result_t rc;
    ev_result_t dispose_rc;

    cmd.duration_ms = duration_ms;
    rc = ev_msg_init_publish(&msg, EV_SYS_GOTO_SLEEP_CMD, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, &cmd, sizeof(cmd));
    }
    if (rc == EV_OK) {
        rc = ev_runtime_graph_publish(&app->graph, &msg, NULL);
    }
    dispose_rc = ev_msg_dispose(&msg);
    return (rc == EV_OK) ? dispose_rc : rc;
}

static ev_result_t publish_diag_request(ev_demo_app_t *app)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_result_t rc;
    ev_result_t dispose_rc;

    rc = ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_runtime_graph_publish(&app->graph, &msg, NULL);
    }
    dispose_rc = ev_msg_dispose(&msg);
    return (rc == EV_OK) ? dispose_rc : rc;
}

static void test_sleep_accepted_only_when_quiescent(void)
{
    sleep_fixture_t fx;

    sleep_fixture_init(&fx);
    drive_until_quiescent(&fx);
    assert(ev_demo_app_pending(&fx.app) == 0U);
    assert(publish_sleep(&fx.app, 42U) == EV_OK);
    {
        ev_actor_runtime_t *power_runtime = ev_runtime_graph_get_runtime(&fx.app.graph, ACT_POWER);
        assert(power_runtime != NULL);
        assert(ev_actor_runtime_step(power_runtime) == EV_OK);
    }
    assert(fx.fake_system.prepare_for_sleep_calls == 1U);
    assert(fx.fake_system.deep_sleep_calls == 1U);
    assert(fx.app.power_ctx.sleep_requests_accepted == 1U);
    assert(fx.app.power_ctx.sleep_requests_rejected == 0U);
}

static void test_sleep_rejected_when_mailbox_work_pending(void)
{
    sleep_fixture_t fx;

    sleep_fixture_init(&fx);
    drive_until_quiescent(&fx);
    assert(publish_diag_request(&fx.app) == EV_OK);
    assert(publish_sleep(&fx.app, 42U) == EV_OK);
    {
        ev_actor_runtime_t *power_runtime = ev_runtime_graph_get_runtime(&fx.app.graph, ACT_POWER);
        assert(power_runtime != NULL);
        assert(ev_actor_runtime_step(power_runtime) == EV_ERR_STATE);
    }
    assert(fx.fake_system.prepare_for_sleep_calls == 0U);
    assert(fx.fake_system.deep_sleep_calls == 0U);
    assert(fx.app.power_ctx.sleep_requests_rejected == 1U);
    assert(fx.app.power_ctx.last_reject_reason == EV_POWER_SLEEP_REJECT_NOT_QUIESCENT);
    assert(fx.app.power_ctx.last_quiescence_report.pending_actor_messages > 0U);
}

static void test_sleep_rejected_when_irq_sample_pending(void)
{
    sleep_fixture_t fx;
    ev_irq_sample_t sample = {
        .line_id = 0U,
        .edge = EV_IRQ_EDGE_FALLING,
        .level = 0U,
        .timestamp_us = 123U,
    };

    sleep_fixture_init(&fx);
    drive_until_quiescent(&fx);
    assert(fake_irq_port_push(&fx.fake_irq, &sample) == EV_OK);
    assert(publish_sleep(&fx.app, 42U) == EV_OK);
    {
        ev_actor_runtime_t *power_runtime = ev_runtime_graph_get_runtime(&fx.app.graph, ACT_POWER);
        assert(power_runtime != NULL);
        assert(ev_actor_runtime_step(power_runtime) == EV_ERR_STATE);
    }
    assert(fx.fake_system.prepare_for_sleep_calls == 0U);
    assert(fx.fake_system.deep_sleep_calls == 0U);
    assert(fx.app.power_ctx.sleep_requests_rejected == 1U);
    assert(fx.app.power_ctx.last_quiescence_report.pending_irq_samples == 1U);
}

int main(void)
{
    test_sleep_accepted_only_when_quiescent();
    test_sleep_rejected_when_mailbox_work_pending();
    test_sleep_rejected_when_irq_sample_pending();
    return 0;
}
