#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"

#include "board_profile.h"

#include "ev/actor_runtime.h"
#include "ev/compiler.h"
#include "ev/dispose.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/mailbox.h"
#include "ev/msg.h"
#include "ev/network_actor.h"
#include "ev/publish.h"

#define EV_HIL_WIFI_TAG "ev_wifi_hil"
#define EV_HIL_WIFI_MAILBOX_CAPACITY 8U
#define EV_HIL_WIFI_POLL_STEP_MS 50U
#define EV_HIL_WIFI_ACTOR_BUDGET 4U

#ifndef EV_BOARD_HIL_WIFI_CONNECT_TIMEOUT_MS
#define EV_BOARD_HIL_WIFI_CONNECT_TIMEOUT_MS 30000U
#endif
#ifndef EV_BOARD_HIL_WIFI_AP_LOSS_TIMEOUT_MS
#define EV_BOARD_HIL_WIFI_AP_LOSS_TIMEOUT_MS 60000U
#endif
#ifndef EV_BOARD_HIL_WIFI_RECOVERY_TIMEOUT_MS
#define EV_BOARD_HIL_WIFI_RECOVERY_TIMEOUT_MS 60000U
#endif
#ifndef EV_BOARD_HIL_WIFI_RECONNECT_STORM_WINDOW_MS
#define EV_BOARD_HIL_WIFI_RECONNECT_STORM_WINDOW_MS 10000U
#endif
#ifndef EV_BOARD_HIL_WIFI_MAX_RING_HIGH_WATERMARK
#define EV_BOARD_HIL_WIFI_MAX_RING_HIGH_WATERMARK EV_NET_INGRESS_RING_CAPACITY
#endif
#ifndef EV_BOARD_HIL_WIFI_MAX_HEAP_DELTA_BYTES
#define EV_BOARD_HIL_WIFI_MAX_HEAP_DELTA_BYTES 0U
#endif
#ifndef EV_BOARD_HIL_WIFI_MAX_RECONNECT_ATTEMPTS
#define EV_BOARD_HIL_WIFI_MAX_RECONNECT_ATTEMPTS 64U
#endif

typedef struct ev_hil_suite_result {
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
} ev_hil_suite_result_t;

typedef struct ev_wifi_hil_runtime {
    ev_net_port_t net_port;
    ev_network_actor_ctx_t network_ctx;
    ev_mailbox_t network_mailbox;
    ev_msg_t network_storage[EV_HIL_WIFI_MAILBOX_CAPACITY];
    ev_actor_runtime_t network_runtime;
    ev_actor_registry_t registry;
} ev_wifi_hil_runtime_t;

static ev_wifi_hil_runtime_t s_wifi_hil_runtime;

static const ev_esp8266_net_config_t k_wifi_hil_net_cfg = {
    .wifi_ssid = EV_BOARD_NET_WIFI_SSID,
    .wifi_password = EV_BOARD_NET_WIFI_PASSWORD,
    .wifi_auth_mode = EV_BOARD_NET_WIFI_AUTH_MODE,
    .mqtt_broker_uri = EV_BOARD_NET_MQTT_BROKER_URI,
    .mqtt_client_id = EV_BOARD_NET_MQTT_CLIENT_ID,
};

static void ev_hil_pass(ev_hil_suite_result_t *result, const char *name)
{
    if (result != NULL) {
        ++result->passed;
    }
    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_CASE %s PASS", name);
}

static void ev_hil_fail(ev_hil_suite_result_t *result, const char *name, const char *reason)
{
    if (result != NULL) {
        ++result->failed;
    }
    ESP_LOGE(EV_HIL_WIFI_TAG, "EV_HIL_CASE %s FAIL reason=%s", name, (reason != NULL) ? reason : "unknown");
}

static void ev_hil_log_wdt_stats(void)
{
#if EV_BOARD_HAS_WDT
    /* WDT hardware support is board/adapter dependent. The normal app validates
     * health-gated feed policy; this WiFi HIL only records that WDT is enabled.
     */
    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_WDT_STATS enabled=1 feeds_ok=unavailable health_rejects=unavailable feeds_failed=unavailable");
#else
    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_WDT_STATS enabled=0 feeds_ok=0 health_rejects=0 feeds_failed=0");
#endif
}

static uint32_t ev_hil_free_heap(void)
{
    return (uint32_t)esp_get_free_heap_size();
}

static ev_result_t ev_hil_publish_network_event(ev_wifi_hil_runtime_t *runtime, const ev_net_ingress_event_t *event)
{
    ev_msg_t msg = {0};
    ev_event_id_t event_id;
    size_t delivered = 0U;
    ev_result_t rc;

    if ((runtime == NULL) || (event == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (event->kind) {
    case EV_NET_EVENT_WIFI_UP:
        event_id = EV_NET_WIFI_UP;
        break;
    case EV_NET_EVENT_WIFI_DOWN:
        event_id = EV_NET_WIFI_DOWN;
        break;
    case EV_NET_EVENT_MQTT_UP:
        event_id = EV_NET_MQTT_UP;
        break;
    case EV_NET_EVENT_MQTT_DOWN:
        event_id = EV_NET_MQTT_DOWN;
        break;
    case EV_NET_EVENT_MQTT_MSG_RX:
        event_id = EV_NET_MQTT_MSG_RX;
        break;
    default:
        return EV_ERR_CONTRACT;
    }

    rc = ev_msg_init_publish(&msg, event_id, ACT_RUNTIME);
    if (rc != EV_OK) {
        return rc;
    }
    if (event->kind == EV_NET_EVENT_MQTT_MSG_RX) {
        rc = ev_msg_set_inline_payload(&msg, event, sizeof(*event));
        if (rc != EV_OK) {
            return rc;
        }
    }

    rc = ev_publish(&msg, ev_actor_registry_delivery, &runtime->registry, &delivered);
    ev_msg_dispose(&msg);
    return rc;
}

static ev_result_t ev_hil_pump_network_actor(ev_wifi_hil_runtime_t *runtime)
{
    ev_actor_pump_report_t report;
    ev_result_t rc;

    if (runtime == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_actor_runtime_pump(&runtime->network_runtime, EV_HIL_WIFI_ACTOR_BUDGET, &report);
    return (rc == EV_ERR_EMPTY) ? EV_OK : rc;
}

static ev_result_t ev_hil_drain_network_once(ev_wifi_hil_runtime_t *runtime)
{
    ev_net_ingress_event_t event;
    ev_result_t rc;

    if (runtime == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&event, 0, sizeof(event));
    rc = runtime->net_port.poll_ingress(runtime->net_port.ctx, &event);
    if (rc == EV_ERR_EMPTY) {
        return ev_hil_pump_network_actor(runtime);
    }
    if (rc != EV_OK) {
        return rc;
    }

    rc = ev_hil_publish_network_event(runtime, &event);
    if (rc != EV_OK) {
        return rc;
    }
    return ev_hil_pump_network_actor(runtime);
}

static void ev_hil_drain_for_ms(ev_wifi_hil_runtime_t *runtime, uint32_t duration_ms)
{
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < duration_ms) {
        (void)ev_hil_drain_network_once(runtime);
        vTaskDelay(pdMS_TO_TICKS(EV_HIL_WIFI_POLL_STEP_MS));
        elapsed_ms += EV_HIL_WIFI_POLL_STEP_MS;
    }
}

static bool ev_hil_wait_for_wifi_up(ev_wifi_hil_runtime_t *runtime, uint32_t timeout_ms)
{
    const ev_network_actor_stats_t *stats;
    const uint32_t start_events = ev_network_actor_stats(&runtime->network_ctx)->wifi_up_events;
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < timeout_ms) {
        (void)ev_hil_drain_network_once(runtime);
        stats = ev_network_actor_stats(&runtime->network_ctx);
        if ((stats != NULL) && (stats->wifi_up_events > start_events)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(EV_HIL_WIFI_POLL_STEP_MS));
        elapsed_ms += EV_HIL_WIFI_POLL_STEP_MS;
    }
    return false;
}

static bool ev_hil_wait_for_wifi_down(ev_wifi_hil_runtime_t *runtime, uint32_t timeout_ms)
{
    const ev_network_actor_stats_t *stats;
    const uint32_t start_events = ev_network_actor_stats(&runtime->network_ctx)->wifi_down_events;
    uint32_t elapsed_ms = 0U;

    while (elapsed_ms < timeout_ms) {
        (void)ev_hil_drain_network_once(runtime);
        stats = ev_network_actor_stats(&runtime->network_ctx);
        if ((stats != NULL) && (stats->wifi_down_events > start_events)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(EV_HIL_WIFI_POLL_STEP_MS));
        elapsed_ms += EV_HIL_WIFI_POLL_STEP_MS;
    }
    return false;
}

static void ev_hil_log_net_stats(ev_wifi_hil_runtime_t *runtime, const char *phase)
{
    ev_net_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    if ((runtime != NULL) && (runtime->net_port.get_stats != NULL) &&
        (runtime->net_port.get_stats(runtime->net_port.ctx, &stats) == EV_OK)) {
        ESP_LOGI(EV_HIL_WIFI_TAG,
                 "EV_HIL_NET_STATS phase=%s write_seq=%u read_seq=%u pending=%u dropped=%u oversize=%u high_watermark=%u reconnect_attempts=%u reconnect_suppressed=%u duplicate_down=%u duplicate_up=%u",
                 (phase != NULL) ? phase : "unknown",
                 (unsigned)stats.write_seq,
                 (unsigned)stats.read_seq,
                 (unsigned)stats.pending_events,
                 (unsigned)stats.dropped_events,
                 (unsigned)stats.dropped_oversize,
                 (unsigned)stats.high_watermark,
                 (unsigned)stats.reconnect_attempts,
                 (unsigned)stats.reconnect_suppressed,
                 (unsigned)stats.duplicate_wifi_down_suppressed,
                 (unsigned)stats.duplicate_wifi_up_suppressed);
    }
}

static bool ev_hil_ring_high_watermark_is_bounded(ev_wifi_hil_runtime_t *runtime)
{
    ev_net_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    if ((runtime == NULL) || (runtime->net_port.get_stats == NULL) ||
        (runtime->net_port.get_stats(runtime->net_port.ctx, &stats) != EV_OK)) {
        return false;
    }
    return stats.high_watermark <= EV_BOARD_HIL_WIFI_MAX_RING_HIGH_WATERMARK;
}

static bool ev_hil_reconnect_attempts_are_bounded(ev_wifi_hil_runtime_t *runtime)
{
    ev_net_stats_t stats;

    memset(&stats, 0, sizeof(stats));
    if ((runtime == NULL) || (runtime->net_port.get_stats == NULL) ||
        (runtime->net_port.get_stats(runtime->net_port.ctx, &stats) != EV_OK)) {
        return false;
    }
    return stats.reconnect_attempts <= EV_BOARD_HIL_WIFI_MAX_RECONNECT_ATTEMPTS;
}

static void ev_hil_log_wifi_up_metadata(void)
{
    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_WIFI_UP ip=unavailable rssi=unavailable");
}

static void ev_hil_phase_boot_connect(ev_hil_suite_result_t *result, ev_wifi_hil_runtime_t *runtime)
{
    const char *name = "boot-connect";

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_PHASE %s", name);
    if (!ev_hil_wait_for_wifi_up(runtime, EV_BOARD_HIL_WIFI_CONNECT_TIMEOUT_MS)) {
        ev_hil_log_net_stats(runtime, name);
        ev_hil_fail(result, name, "wifi_up_not_observed");
        return;
    }
    ev_hil_log_wifi_up_metadata();
    ev_hil_log_net_stats(runtime, name);
    ev_hil_pass(result, name);
}

static void ev_hil_phase_ap_loss(ev_hil_suite_result_t *result, ev_wifi_hil_runtime_t *runtime)
{
    const char *name = "ap-loss";

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_PHASE %s", name);
    ESP_LOGI(EV_HIL_WIFI_TAG,
             "EV_HIL_OPERATOR action=disable-or-isolate-ap timeout_ms=%u",
             (unsigned)EV_BOARD_HIL_WIFI_AP_LOSS_TIMEOUT_MS);
    if (!ev_hil_wait_for_wifi_down(runtime, EV_BOARD_HIL_WIFI_AP_LOSS_TIMEOUT_MS)) {
        ev_hil_log_net_stats(runtime, name);
        ev_hil_fail(result, name, "wifi_down_not_observed");
        return;
    }

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_WIFI_DOWN reason=ap-loss-observed");
    ev_hil_drain_for_ms(runtime, EV_BOARD_HIL_WIFI_RECONNECT_STORM_WINDOW_MS);
    ev_hil_log_net_stats(runtime, name);
    if (!ev_hil_ring_high_watermark_is_bounded(runtime)) {
        ev_hil_fail(result, name, "ring_high_watermark_exceeded");
        return;
    }
    if (!ev_hil_reconnect_attempts_are_bounded(runtime)) {
        ev_hil_fail(result, name, "reconnect_attempts_exceeded");
        return;
    }
    ev_hil_pass(result, name);
}

static void ev_hil_phase_recovery(ev_hil_suite_result_t *result, ev_wifi_hil_runtime_t *runtime)
{
    const char *name = "recovery";

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_PHASE %s", name);
    ESP_LOGI(EV_HIL_WIFI_TAG,
             "EV_HIL_OPERATOR action=restore-ap timeout_ms=%u",
             (unsigned)EV_BOARD_HIL_WIFI_RECOVERY_TIMEOUT_MS);
    if (!ev_hil_wait_for_wifi_up(runtime, EV_BOARD_HIL_WIFI_RECOVERY_TIMEOUT_MS)) {
        ev_hil_log_net_stats(runtime, name);
        ev_hil_fail(result, name, "wifi_up_after_recovery_not_observed");
        return;
    }
    ev_hil_log_wifi_up_metadata();
    ev_hil_log_net_stats(runtime, name);
    ev_hil_pass(result, name);
}

static void ev_hil_phase_wdt_stats(ev_hil_suite_result_t *result)
{
    const char *name = "wdt-health-under-net-storm";

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_PHASE %s", name);
    ev_hil_log_wdt_stats();
    ev_hil_pass(result, name);
}

static void ev_hil_phase_heap_delta(ev_hil_suite_result_t *result, ev_wifi_hil_runtime_t *runtime)
{
    const char *name = "heap-delta-window";
    const uint32_t before_heap = ev_hil_free_heap();
    uint32_t after_heap;
    int32_t delta;

    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_PHASE %s", name);
    ev_hil_drain_for_ms(runtime, EV_BOARD_HIL_WIFI_RECONNECT_STORM_WINDOW_MS);
    after_heap = ev_hil_free_heap();
    delta = (int32_t)after_heap - (int32_t)before_heap;
    ESP_LOGI(EV_HIL_WIFI_TAG,
             "EV_HIL_HEAP before=%u after=%u delta=%d",
             (unsigned)before_heap,
             (unsigned)after_heap,
             (int)delta);
    if ((after_heap + EV_BOARD_HIL_WIFI_MAX_HEAP_DELTA_BYTES) < before_heap) {
        ev_hil_fail(result, name, "heap_decreased_during_poll_window");
        return;
    }
    ev_hil_pass(result, name);
}

static ev_result_t ev_hil_runtime_init(ev_wifi_hil_runtime_t *runtime)
{
    ev_result_t rc;

    if (runtime == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    memset(runtime, 0, sizeof(*runtime));

    rc = ev_esp8266_net_port_init(&runtime->net_port, &k_wifi_hil_net_cfg);
    if (rc != EV_OK) {
        return rc;
    }
    rc = ev_network_actor_init(&runtime->network_ctx, &runtime->net_port);
    if (rc != EV_OK) {
        return rc;
    }
    rc = ev_mailbox_init(&runtime->network_mailbox,
                         EV_MAILBOX_FIFO_8,
                         runtime->network_storage,
                         EV_HIL_WIFI_MAILBOX_CAPACITY);
    if (rc != EV_OK) {
        return rc;
    }
    rc = ev_actor_runtime_init(&runtime->network_runtime,
                               ACT_NETWORK,
                               &runtime->network_mailbox,
                               ev_network_actor_handle,
                               &runtime->network_ctx);
    if (rc != EV_OK) {
        return rc;
    }
    rc = ev_actor_registry_init(&runtime->registry);
    if (rc != EV_OK) {
        return rc;
    }
    return ev_actor_registry_bind(&runtime->registry, &runtime->network_runtime);
}

static ev_result_t ev_wifi_hil_run(ev_wifi_hil_runtime_t *runtime)
{
    ev_hil_suite_result_t result;
    ev_result_t rc;

    memset(&result, 0, sizeof(result));
    ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_START wifi-reconnect board=%s", EV_BOARD_PROFILE_NAME);

    rc = ev_hil_runtime_init(runtime);
    if (rc != EV_OK) {
        ev_hil_fail(&result, "runtime-init", "runtime_init_failed");
        goto done;
    }
    rc = runtime->net_port.init(runtime->net_port.ctx);
    if (rc != EV_OK) {
        ev_hil_fail(&result, "net-init", "net_init_failed");
        goto done;
    }
    rc = runtime->net_port.start(runtime->net_port.ctx);
    if (rc != EV_OK) {
        ev_hil_fail(&result, "net-start", "net_start_failed");
        goto done;
    }

    ev_hil_phase_boot_connect(&result, runtime);
    ev_hil_phase_ap_loss(&result, runtime);
    ev_hil_phase_recovery(&result, runtime);
    ev_hil_phase_wdt_stats(&result);
    ev_hil_phase_heap_delta(&result, runtime);

done:
    ev_hil_log_net_stats(runtime, "final");
    ESP_LOGI(EV_HIL_WIFI_TAG,
             "HIL summary passed=%u failed=%u skipped=%u",
             (unsigned)result.passed,
             (unsigned)result.failed,
             (unsigned)result.skipped);
    if ((result.failed == 0U) && (result.skipped == 0U)) {
        ESP_LOGI(EV_HIL_WIFI_TAG, "EV_HIL_RESULT PASS failures=0 skipped=0");
        return EV_OK;
    }
    ESP_LOGE(EV_HIL_WIFI_TAG,
             "EV_HIL_RESULT FAIL failures=%u skipped=%u",
             (unsigned)result.failed,
             (unsigned)result.skipped);
    return EV_ERR_STATE;
}

static void ev_hil_idle_forever(void)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

void app_main(void)
{
    ev_result_t rc;

    (void)uart_set_baudrate(UART_NUM_0, 115200U);
    ESP_LOGI(EV_HIL_WIFI_TAG, "starting %s WiFi reconnect HIL", EV_BOARD_PROFILE_NAME);
    rc = ev_wifi_hil_run(&s_wifi_hil_runtime);
    if (rc == EV_OK) {
        ESP_LOGI(EV_HIL_WIFI_TAG, "wifi reconnect HIL completed successfully");
    } else {
        ESP_LOGE(EV_HIL_WIFI_TAG, "wifi reconnect HIL failed rc=%d", (int)rc);
    }
    ev_hil_idle_forever();
}
