#include <assert.h>
#include <stddef.h>

#include "ev/actor_runtime.h"
#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/send.h"

static ev_result_t ok_handler(void *actor_context, const ev_msg_t *msg)
{
    size_t *calls = (size_t *)actor_context;
    (void)msg;
    ++(*calls);
    return EV_OK;
}

static ev_result_t fail_handler(void *actor_context, const ev_msg_t *msg)
{
    size_t *calls = (size_t *)actor_context;
    (void)msg;
    ++(*calls);
    return EV_ERR_STATE;
}

int main(void)
{
    ev_msg_t diag_storage[8] = {{0}};
    ev_msg_t app_storage[8] = {{0}};
    ev_msg_t mcp_storage[8] = {{0}};
    ev_msg_t rtc_storage[8] = {{0}};
    ev_msg_t ds18b20_storage[8] = {{0}};
    ev_msg_t oled_storage[8] = {{0}};
    ev_msg_t supervisor_storage[8] = {{0}};
    ev_msg_t diag_storage_partial[8] = {{0}};
    ev_msg_t diag_storage_fail[8] = {{0}};
    ev_mailbox_t diag_mailbox;
    ev_mailbox_t app_mailbox;
    ev_mailbox_t mcp_mailbox;
    ev_mailbox_t rtc_mailbox;
    ev_mailbox_t ds18b20_mailbox;
    ev_mailbox_t oled_mailbox;
    ev_mailbox_t supervisor_mailbox;
    ev_mailbox_t diag_mailbox_partial;
    ev_mailbox_t diag_mailbox_fail;
    ev_actor_runtime_t diag_runtime;
    ev_actor_runtime_t app_runtime;
    ev_actor_runtime_t mcp_runtime;
    ev_actor_runtime_t rtc_runtime;
    ev_actor_runtime_t ds18b20_runtime;
    ev_actor_runtime_t oled_runtime;
    ev_actor_runtime_t supervisor_runtime;
    ev_actor_runtime_t diag_runtime_partial;
    ev_actor_runtime_t diag_runtime_fail;
    ev_actor_registry_t registry = {0};
    ev_actor_registry_t partial_registry = {0};
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_publish_report_t report;
    const ev_actor_registry_stats_t *registry_stats;
    const ev_actor_runtime_stats_t *diag_stats;
    const ev_actor_runtime_stats_t *app_stats;
    const ev_actor_runtime_stats_t *partial_stats;
    const ev_actor_runtime_stats_t *fail_stats;
    size_t diag_calls = 0U;
    size_t app_calls = 0U;
    size_t mcp_calls = 0U;
    size_t rtc_calls = 0U;
    size_t ds18b20_calls = 0U;
    size_t oled_calls = 0U;
    size_t supervisor_calls = 0U;
    size_t fail_calls = 0U;
    size_t i;

    assert(ev_mailbox_init(&diag_mailbox, EV_MAILBOX_FIFO_8, diag_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&app_mailbox, EV_MAILBOX_FIFO_8, app_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&mcp_mailbox, EV_MAILBOX_FIFO_8, mcp_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&rtc_mailbox, EV_MAILBOX_FIFO_8, rtc_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&ds18b20_mailbox, EV_MAILBOX_FIFO_8, ds18b20_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&oled_mailbox, EV_MAILBOX_FIFO_8, oled_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&supervisor_mailbox, EV_MAILBOX_FIFO_8, supervisor_storage, 8U) == EV_OK);
    assert(ev_mailbox_init(&diag_mailbox_partial, EV_MAILBOX_FIFO_8, diag_storage_partial, 8U) == EV_OK);
    assert(ev_mailbox_init(&diag_mailbox_fail, EV_MAILBOX_FIFO_8, diag_storage_fail, 8U) == EV_OK);

    assert(ev_actor_runtime_init(&diag_runtime, ACT_DIAG, &diag_mailbox, ok_handler, &diag_calls) == EV_OK);
    assert(ev_actor_runtime_init(&app_runtime, ACT_APP, &app_mailbox, ok_handler, &app_calls) == EV_OK);
    assert(ev_actor_runtime_init(&mcp_runtime, ACT_MCP23008, &mcp_mailbox, ok_handler, &mcp_calls) == EV_OK);
    assert(ev_actor_runtime_init(&rtc_runtime, ACT_RTC, &rtc_mailbox, ok_handler, &rtc_calls) == EV_OK);
    assert(ev_actor_runtime_init(&ds18b20_runtime, ACT_DS18B20, &ds18b20_mailbox, ok_handler, &ds18b20_calls) == EV_OK);
    assert(ev_actor_runtime_init(&oled_runtime, ACT_OLED, &oled_mailbox, ok_handler, &oled_calls) == EV_OK);
    assert(ev_actor_runtime_init(&supervisor_runtime, ACT_SUPERVISOR, &supervisor_mailbox, ok_handler, &supervisor_calls) == EV_OK);
    assert(ev_actor_runtime_init(&diag_runtime_partial, ACT_DIAG, &diag_mailbox_partial, ok_handler, &diag_calls) == EV_OK);
    assert(ev_actor_runtime_init(&diag_runtime_fail, ACT_DIAG, &diag_mailbox_fail, fail_handler, &fail_calls) == EV_OK);

    assert(ev_actor_registry_init(&registry) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &diag_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &app_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &mcp_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &rtc_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &ds18b20_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &oled_runtime) == EV_OK);
    assert(ev_actor_registry_bind(&registry, &supervisor_runtime) == EV_OK);

    registry_stats = ev_actor_registry_stats(&registry);
    assert(registry_stats != NULL);
    assert(registry_stats->delivery_attempted == 0U);
    assert(registry_stats->delivery_succeeded == 0U);
    assert(registry_stats->delivery_failed == 0U);
    assert(registry_stats->delivery_target_missing == 0U);
    assert(registry_stats->last_target_actor == EV_ACTOR_NONE);
    assert(registry_stats->last_result == EV_OK);

    diag_stats = ev_actor_runtime_stats(&diag_runtime);
    assert(diag_stats != NULL);
    assert(diag_stats->enqueued == 0U);
    assert(diag_stats->pending_high_watermark == 0U);
    assert(diag_stats->pump_calls == 0U);
    assert(diag_stats->pump_budget_hits == 0U);
    assert(diag_stats->last_result == EV_OK);

    assert(ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT) == EV_OK);
    assert(ev_publish(&msg, ev_actor_registry_delivery, &registry, NULL) == EV_OK);

    registry_stats = ev_actor_registry_stats(&registry);
    assert(registry_stats->delivery_attempted == 7U);
    assert(registry_stats->delivery_succeeded == 7U);
    assert(registry_stats->delivery_failed == 0U);
    assert(registry_stats->delivery_target_missing == 0U);
    assert(registry_stats->last_target_actor == ACT_SUPERVISOR);
    assert(registry_stats->last_result == EV_OK);

    diag_stats = ev_actor_runtime_stats(&diag_runtime);
    app_stats = ev_actor_runtime_stats(&app_runtime);
    assert(diag_stats->enqueued == 1U);
    assert(app_stats->enqueued == 1U);
    assert(diag_stats->pending_high_watermark == 1U);
    assert(app_stats->pending_high_watermark == 1U);
    assert(diag_stats->enqueue_failed == 0U);
    assert(ev_actor_runtime_stats(&mcp_runtime)->enqueued == 1U);
    assert(ev_actor_runtime_stats(&rtc_runtime)->enqueued == 1U);
    assert(ev_actor_runtime_stats(&ds18b20_runtime)->enqueued == 1U);
    assert(ev_actor_runtime_stats(&oled_runtime)->enqueued == 1U);
    assert(ev_actor_runtime_stats(&supervisor_runtime)->enqueued == 1U);

    assert(ev_actor_runtime_step(&diag_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&app_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&mcp_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&rtc_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&ds18b20_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&oled_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&supervisor_runtime) == EV_OK);
    assert(ev_actor_runtime_step(&app_runtime) == EV_ERR_EMPTY);
    assert(diag_calls == 1U);
    assert(app_calls == 1U);
    assert(mcp_calls == 1U);
    assert(rtc_calls == 1U);
    assert(ds18b20_calls == 1U);
    assert(oled_calls == 1U);
    assert(supervisor_calls == 1U);

    diag_stats = ev_actor_runtime_stats(&diag_runtime);
    app_stats = ev_actor_runtime_stats(&app_runtime);
    assert(diag_stats->steps_ok == 1U);
    assert(diag_stats->handler_errors == 0U);
    assert(diag_stats->dispose_errors == 0U);
    assert(diag_stats->last_result == EV_OK);
    assert(app_stats->steps_ok == 1U);
    assert(app_stats->steps_empty == 1U);
    assert(app_stats->last_result == EV_ERR_EMPTY);

    assert(ev_actor_registry_reset_stats(&registry) == EV_OK);
    registry_stats = ev_actor_registry_stats(&registry);
    assert(registry_stats->delivery_attempted == 0U);
    assert(registry_stats->last_target_actor == EV_ACTOR_NONE);
    assert(registry_stats->last_result == EV_OK);

    assert(ev_actor_registry_init(&partial_registry) == EV_OK);
    assert(ev_actor_registry_bind(&partial_registry, &diag_runtime_partial) == EV_OK);
    ev_publish_report_reset(&report);
    assert(ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT) == EV_OK);
    assert(ev_publish_ex(&msg, ev_actor_registry_delivery, &partial_registry, EV_PUBLISH_BEST_EFFORT, &report) == EV_ERR_PARTIAL);
    assert(report.matched_routes == 7U);
    assert(report.delivered_count == 1U);
    assert(report.failed_count == 6U);
    assert(report.first_failed_actor == ACT_APP);
    assert(report.first_error == EV_ERR_NOT_FOUND);

    registry_stats = ev_actor_registry_stats(&partial_registry);
    assert(registry_stats->delivery_attempted == 7U);
    assert(registry_stats->delivery_succeeded == 1U);
    assert(registry_stats->delivery_failed == 6U);
    assert(registry_stats->delivery_target_missing == 6U);
    assert(registry_stats->last_target_actor == ACT_SUPERVISOR);
    assert(registry_stats->last_result == EV_ERR_NOT_FOUND);

    partial_stats = ev_actor_runtime_stats(&diag_runtime_partial);
    assert(partial_stats->enqueued == 1U);
    assert(partial_stats->pending_high_watermark == 1U);

    assert(ev_mailbox_reset(&diag_mailbox_partial) == EV_OK);
    assert(ev_actor_runtime_reset_stats(&diag_runtime_partial) == EV_OK);
    partial_stats = ev_actor_runtime_stats(&diag_runtime_partial);
    assert(partial_stats->enqueued == 0U);
    assert(partial_stats->pending_high_watermark == 0U);
    assert(partial_stats->last_result == EV_OK);

    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    for (i = 0U; i < 8U; ++i) {
        assert(ev_send(ACT_DIAG, &msg, ev_actor_registry_delivery, &partial_registry) == EV_OK);
    }
    assert(ev_send(ACT_DIAG, &msg, ev_actor_registry_delivery, &partial_registry) == EV_ERR_FULL);
    partial_stats = ev_actor_runtime_stats(&diag_runtime_partial);
    assert(partial_stats->enqueued == 8U);
    assert(partial_stats->enqueue_failed == 1U);
    assert(partial_stats->pending_high_watermark == 8U);
    assert(partial_stats->last_result == EV_ERR_FULL);
    assert(partial_stats->pump_calls == 0U);

    assert(ev_actor_registry_init(&partial_registry) == EV_OK);
    assert(ev_actor_registry_bind(&partial_registry, &diag_runtime_fail) == EV_OK);
    assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    assert(ev_send(ACT_DIAG, &msg, ev_actor_registry_delivery, &partial_registry) == EV_OK);
    assert(ev_actor_runtime_step(&diag_runtime_fail) == EV_ERR_STATE);
    fail_stats = ev_actor_runtime_stats(&diag_runtime_fail);
    assert(fail_calls == 1U);
    assert(fail_stats->enqueued == 1U);
    assert(fail_stats->steps_ok == 0U);
    assert(fail_stats->handler_errors == 1U);
    assert(fail_stats->dispose_errors == 0U);
    assert(fail_stats->last_result == EV_ERR_STATE);

    return 0;
}
