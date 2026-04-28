#include <assert.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ev/demo_app.h"
#include "ev/dispose.h"
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


static ev_result_t noop_deliver(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    (void)target_actor;
    (void)msg;
    (void)ctx;
    return EV_OK;
}

static ev_result_t noop_retain(void *ctx, const void *data, size_t size)
{
    (void)ctx;
    (void)data;
    (void)size;
    return EV_OK;
}

static void noop_release(void *ctx, const void *data, size_t size)
{
    (void)ctx;
    (void)data;
    (void)size;
}

static void drive_app_until_quiescent(ev_demo_app_t *app, fake_irq_port_t *fake_irq, unsigned max_iterations)
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

    assert(!"demo app did not quiesce within iteration budget");
}


static void test_oled_commit_semantics(void)
{
    fake_i2c_port_t fake_i2c;
    ev_i2c_port_t i2c_port = {0};
    ev_oled_actor_ctx_t oled;
    ev_msg_t msg = {0};
    ev_oled_scene_t scene = {0};
    uint32_t baseline_flushes;
    uint32_t baseline_writes;

    fake_i2c_port_init(&fake_i2c);
    fake_i2c_port_set_present(&fake_i2c, EV_OLED_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_bind(&i2c_port, &fake_i2c);

    assert(ev_oled_actor_init(&oled,
                              &i2c_port,
                              EV_I2C_PORT_NUM_0,
                              EV_OLED_DEFAULT_ADDR_7BIT,
                              EV_OLED_CONTROLLER_SSD1306,
                              noop_deliver,
                              &oled) == EV_OK);

    assert(ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT) == EV_OK);
    assert(ev_oled_actor_handle(&oled, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);

    baseline_flushes = oled.stats.flush_attempts;
    baseline_writes = fake_i2c.write_stream_calls;

    scene.flags = EV_OLED_SCENE_FLAG_VISIBLE;
    scene.page_offset = 0U;
    scene.column_offset = 0U;
    (void)snprintf(scene.lines[0], sizeof(scene.lines[0]), "%s", "ATNEL AIR");
    (void)snprintf(scene.lines[1], sizeof(scene.lines[1]), "%s", "12:34:56");
    (void)snprintf(scene.lines[2], sizeof(scene.lines[2]), "%s", "25.00 C");

    assert(ev_msg_init_publish(&msg, EV_OLED_COMMIT_FRAME, ACT_APP) == EV_OK);
    assert(ev_msg_set_external_payload(&msg,
                                       &scene,
                                       sizeof(scene),
                                       noop_retain,
                                       noop_release,
                                       NULL) == EV_OK);
    assert(ev_oled_actor_handle(&oled, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(oled.pending_flush == false);
    assert(oled.stats.display_commands_seen == 0U);
    assert(oled.stats.flush_attempts > baseline_flushes);
    assert(fake_i2c.write_stream_calls > baseline_writes);

    baseline_flushes = oled.stats.flush_attempts;
    baseline_writes = fake_i2c.write_stream_calls;
    assert(ev_msg_init_publish(&msg, EV_OLED_COMMIT_FRAME, ACT_APP) == EV_OK);
    assert(ev_msg_set_external_payload(&msg,
                                       &scene,
                                       sizeof(scene),
                                       noop_retain,
                                       noop_release,
                                       NULL) == EV_OK);
    assert(ev_oled_actor_handle(&oled, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(oled.stats.flush_attempts == baseline_flushes);
    assert(fake_i2c.write_stream_calls == baseline_writes);

    (void)snprintf(scene.lines[1], sizeof(scene.lines[1]), "%s", "12:34:57");
    assert(ev_msg_init_publish(&msg, EV_OLED_COMMIT_FRAME, ACT_APP) == EV_OK);
    assert(ev_msg_set_external_payload(&msg,
                                       &scene,
                                       sizeof(scene),
                                       noop_retain,
                                       noop_release,
                                       NULL) == EV_OK);
    assert(ev_oled_actor_handle(&oled, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
    assert(oled.stats.flush_attempts > baseline_flushes);
    assert(fake_i2c.write_stream_calls > baseline_writes);
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
    ev_i2c_port_t i2c_port;
    ev_irq_port_t irq_port;
    ev_onewire_port_t onewire_port;
    ev_demo_app_t app;
    ev_demo_app_config_t cfg = {
        .app_tag = "host_demo",
        .board_name = "host",
        .tick_period_ms = 1000U,
        .clock_port = &clock_port,
        .log_port = &log_port,
        .irq_port = &irq_port,
        .i2c_port = &i2c_port,
        .onewire_port = &onewire_port,
        .board_profile = &k_fake_full_board_profile,
    };
    const ev_demo_app_stats_t *stats;
    const ev_system_pump_stats_t *pump_stats;
    uint8_t rtc_time[7];
    uint8_t mcp_gpio = 0xFFU;
    uint8_t scratchpad[9] = {
        0x90U, 0x01U, 0x4BU, 0x46U, 0x7FU, 0xFFU, 0x0CU, 0x10U, 0x00U,
    };
    ev_irq_sample_t irq_sample = {
        .line_id = 0U,
        .edge = EV_IRQ_EDGE_FALLING,
        .level = 0U,
        .timestamp_us = 1234U,
    };

    clock.auto_increment_us = 1000ULL;

    fake_i2c_port_init(&fake_i2c);
    fake_i2c_port_bind(&i2c_port, &fake_i2c);
    fake_i2c_port_set_present(&fake_i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(&fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, true);
    fake_i2c_port_set_present(&fake_i2c, EV_OLED_DEFAULT_ADDR_7BIT, true);

    rtc_time[0] = test_bcd(56U);
    rtc_time[1] = test_bcd(34U);
    rtc_time[2] = test_bcd(12U);
    rtc_time[3] = test_bcd(3U);
    rtc_time[4] = test_bcd(19U);
    rtc_time[5] = test_bcd(3U);
    rtc_time[6] = test_bcd(24U);
    fake_i2c_port_seed_regs(&fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, 0x00U, rtc_time, sizeof(rtc_time));
    fake_i2c_port_seed_regs(&fake_i2c, EV_MCP23008_DEFAULT_ADDR_7BIT, 0x09U, &mcp_gpio, 1U);

    fake_irq_port_init(&fake_irq);
    fake_irq_port_bind(&irq_port, &fake_irq);

    fake_onewire_port_init(&fake_onewire);
    scratchpad[8] = test_ds18b20_crc8(scratchpad, 8U);
    fake_onewire_port_seed_read_bytes(&fake_onewire, scratchpad, sizeof(scratchpad));
    fake_onewire_port_bind(&onewire_port, &fake_onewire);

    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_app_publish_boot(&app) == EV_OK);
    assert(ev_demo_app_pending(&app) == 8U);

    assert(ev_demo_app_poll(&app) == EV_OK);
    assert(ev_demo_app_pending(&app) == 0U);
    stats = ev_demo_app_stats(&app);
    assert(stats != NULL);
    assert(stats->boot_completions == 1U);
    assert(stats->ticks_published == 0U);
    assert(stats->diag_ticks_seen == 0U);
    assert(stats->snapshots_published >= 1U);
    assert(stats->snapshots_received >= 1U);
    assert(stats->publish_errors == 0U);
    assert(stats->pump_errors == 0U);
    assert(stats->max_pending_before_poll >= 6U);
    assert(stats->max_pump_calls_per_poll >= 1U);
    assert(stats->max_turns_per_poll >= 1U);
    assert(stats->max_messages_per_poll >= 1U);
    assert(stats->max_poll_elapsed_ms > 0U);
    assert(app.app_actor.system_ready);
    assert((app.app_actor.active_hardware_mask & EV_SUPERVISOR_HW_MCP23008) != 0U);
    assert((app.app_actor.active_hardware_mask & EV_SUPERVISOR_HW_RTC) != 0U);
    assert((app.app_actor.active_hardware_mask & EV_SUPERVISOR_HW_OLED) != 0U);
    assert(app.mcp23008_ctx.configured);
    assert(app.oled_ctx.state == EV_OLED_STATE_READY);
    assert(app.ds18b20_ctx.conversion_pending);
    assert(fake_irq_port_is_enabled(&fake_irq, 0U));
    assert(fake_i2c.write_regs_calls > 0U);
    assert(fake_i2c.write_stream_calls > 0U);
    assert(app.app_actor.time_valid);
    assert(app.app_actor.last_time.hours == 12U);
    assert(app.app_actor.last_time.minutes == 34U);
    assert(app.app_actor.last_time.seconds == 56U);

    rtc_time[0] = test_bcd(57U);
    fake_i2c_port_seed_regs(&fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, 0x00U, rtc_time, sizeof(rtc_time));
    assert(fake_irq_port_push(&fake_irq, &irq_sample) == EV_OK);
    assert(ev_demo_app_poll(&app) == EV_OK);
    assert(app.app_actor.time_valid);
    assert(app.app_actor.last_time.seconds == 57U);
    stats = ev_demo_app_stats(&app);
    assert(stats->irq_samples_drained >= 1U);
    assert(stats->max_irq_samples_per_poll >= 1U);
    assert(app.rtc_ctx.irq_samples_seen >= 1U);
    assert(app.rtc_ctx.published_updates >= 2U);

    clock.now_us = 1000ULL * 1000ULL;
    drive_app_until_quiescent(&app, &fake_irq, 8U);
    stats = ev_demo_app_stats(&app);
    assert(stats->ticks_published == 1U);
    assert(stats->diag_ticks_seen == 1U);
    assert(app.app_actor.last_snapshot_sequence >= 2U);
    assert(app.app_actor.last_diag_ticks_seen == 1U);
    assert(app.app_actor.temp_valid);
    assert(app.app_actor.last_temp.centi_celsius == 2500);
    assert(app.ds18b20_ctx.sensor_present);
    assert(app.ds18b20_ctx.temp_valid);
    assert(app.ds18b20_ctx.last_centi_celsius == 2500);
    assert(app.ds18b20_ctx.scratchpad_reads_ok >= 1U);
    assert(app.oled_ctx.stats.display_commands_seen == 0U);

    rtc_time[0] = test_bcd(58U);
    fake_i2c_port_seed_regs(&fake_i2c, EV_RTC_DEFAULT_ADDR_7BIT, 0x00U, rtc_time, sizeof(rtc_time));
    clock.now_us = 2000ULL * 1000ULL;
    drive_app_until_quiescent(&app, &fake_irq, 8U);
    assert(app.app_actor.last_time.seconds == 58U);
    assert(app.rtc_ctx.time_valid);
    assert(app.rtc_ctx.fallback_polls >= 1U);

    pump_stats = ev_demo_app_system_pump_stats(&app);
    assert(pump_stats != NULL);
    assert(pump_stats->run_calls >= 1U);
    assert(pump_stats->messages_processed >= 1U);
    assert(pump_stats->last_result == EV_OK);
    assert(log.writes >= 1U);
    assert(strlen(log.last_message) > 0U);

    test_oled_commit_semantics();
    return 0;
}
