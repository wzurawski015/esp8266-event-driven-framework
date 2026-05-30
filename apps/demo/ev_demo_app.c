#include "ev/demo_app.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/capabilities.h"
#include "ev/compiler.h"
#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/runtime_poll.h"
#include "ev/runtime_loop.h"
#include "ev/demo_runtime_instances.h"
#include "ev/demo_internal.h"
#include "ev/demo_board_wiring.h"
#include "ev/demo_policy.h"
#include "ev/demo_presentation.h"
#include "ev/runtime_graph_inspection.h"
#include "ev/runtime_graph_timers.h"

#define EV_DEMO_APP_DEFAULT_TICK_MS 1000U
#define EV_DEMO_APP_TURN_BUDGET 4U
#define EV_APP_POLL_MAX_IRQ_SAMPLES 16U
#define EV_APP_POLL_RESERVED_IRQ_SAMPLES 4U
#define EV_APP_POLL_MAX_NET_SAMPLES 16U
#define EV_APP_POLL_RESERVED_NET_SAMPLES 4U
#define EV_APP_POLL_MAX_MESSAGES 32U
#define EV_APP_POLL_MAX_PUMP_TURNS 10U



void ev_demo_app_logf(ev_demo_app_t *app, ev_log_level_t level, const char *fmt, ...)
{
    char buffer[192];
    va_list ap;
    int len;

    if ((app == NULL) || (app->log_port == NULL) || (app->log_port->write == NULL) || (app->app_tag == NULL) ||
        (fmt == NULL)) {
        return;
    }

    va_start(ap, fmt);
    len = vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (len < 0) {
        return;
    }
    if ((size_t)len >= sizeof(buffer)) {
        len = (int)(sizeof(buffer) - 1U);
        buffer[len] = '\0';
    }

    (void)app->log_port->write(app->log_port->ctx, level, app->app_tag, buffer, (size_t)len);
}

ev_result_t ev_demo_app_now_ms(ev_demo_app_t *app, uint32_t *out_now_ms)
{
    ev_time_mono_us_t now_us;
    ev_result_t rc;

    if ((app == NULL) || (out_now_ms == NULL) || (app->clock_port == NULL) || (app->clock_port->mono_now_us == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = app->clock_port->mono_now_us(app->clock_port->ctx, &now_us);
    if (rc != EV_OK) {
        return rc;
    }

    *out_now_ms = (uint32_t)(now_us / 1000ULL);
    return EV_OK;
}

static ev_result_t ev_demo_app_publish_net_event(ev_demo_app_t *app, const ev_net_ingress_event_t *event);

static void ev_demo_app_record_delivery_report(ev_demo_app_t *app, const ev_delivery_report_t *report)
{
    if ((app == NULL) || (report == NULL)) {
        return;
    }
    app->stats.disabled_route_deliveries += (uint32_t)report->optional_disabled_routes;
    app->stats.watchdog_disabled_route_deliveries += (uint32_t)report->optional_disabled_watchdog_routes;
    app->stats.network_disabled_route_deliveries += (uint32_t)report->optional_disabled_network_routes;
}

ev_result_t ev_demo_app_publish_owned(ev_demo_app_t *app, ev_msg_t *msg)
{
    ev_result_t rc;
    ev_result_t dispose_rc;
    ev_delivery_report_t report;

    if ((app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_runtime_graph_publish(&app->graph, msg, &report);
    ev_demo_app_record_delivery_report(app, &report);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
    }

    dispose_rc = ev_msg_dispose(msg);
    if ((rc == EV_OK) && (dispose_rc != EV_OK)) {
        return dispose_rc;
    }

    return rc;
}



static ev_result_t ev_demo_app_publish_system_event(ev_demo_app_t *app,
                                                 ev_event_id_t event_id,
                                                 const void *payload,
                                                 size_t payload_size)
{
    ev_msg_t msg = {0};
    ev_result_t rc;

    if ((app == NULL) || ((payload == NULL) && (payload_size != 0U))) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_msg_init_publish(&msg, event_id, ACT_BOOT);
    if (rc != EV_OK) {
        return rc;
    }

    if (payload_size > 0U) {
        rc = ev_msg_set_inline_payload(&msg, payload, payload_size);
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            return rc;
        }
    }

    return ev_demo_app_publish_owned(app, &msg);
}

static ev_result_t ev_demo_app_publish_tick(ev_demo_app_t *app)
{
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_demo_app_publish_system_event(app, EV_TICK_1S, NULL, 0U);
    if (rc == EV_OK) {
        ++app->stats.ticks_published;
    }

    return rc;
}

static ev_result_t ev_demo_app_publish_tick_100ms(ev_demo_app_t *app)
{
    return ev_demo_app_publish_system_event(app, EV_TICK_100MS, NULL, 0U);
}

ev_result_t ev_demo_app_publish_panel_led_command(ev_demo_app_t *app, uint8_t value_mask, uint8_t valid_mask)
{
    ev_msg_t msg = {0};
    ev_panel_led_set_cmd_t cmd = {0};
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    cmd.value_mask = (uint8_t)(value_mask & EV_MCP23008_LED_MASK);
    cmd.valid_mask = (uint8_t)(valid_mask & EV_MCP23008_LED_MASK);

    rc = ev_msg_init_publish(&msg, EV_PANEL_LED_SET_CMD, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_msg_set_inline_payload(&msg, &cmd, sizeof(cmd));
    }
    if (rc != EV_OK) {
        (void)ev_msg_dispose(&msg);
        return rc;
    }

    return ev_demo_app_publish_owned(app, &msg);
}

bool ev_demo_app_hardware_active(const ev_demo_app_actor_state_t *state, uint32_t hw_mask)
{
    return (state != NULL) && state->system_ready && ((state->active_hardware_mask & hw_mask) != 0U);
}

static ev_result_t ev_demo_app_publish_irq_sample(ev_demo_app_t *app, const ev_irq_sample_t *sample)
{
    if ((app == NULL) || (sample == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    return ev_demo_app_publish_system_event(app, EV_GPIO_IRQ, sample, sizeof(*sample));
}


ev_result_t ev_demo_app_publish_oled_scene_commit(ev_demo_app_t *app, const ev_oled_scene_t *scene)
{
    ev_lease_handle_t handle = {0};
    ev_msg_t msg = {0};
    void *data = NULL;
    ev_result_t rc;

    if ((app == NULL) || (scene == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    rc = ev_lease_pool_acquire(&app->lease_pool, sizeof(*scene), &handle, &data);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
        ev_demo_app_logf(app, EV_LOG_ERROR, "oled scene acquire failed rc=%d", (int)rc);
        return rc;
    }

    memcpy(data, scene, sizeof(*scene));

    rc = ev_msg_init_publish(&msg, EV_OLED_COMMIT_FRAME, ACT_APP);
    if (rc == EV_OK) {
        rc = ev_lease_pool_attach_msg(&msg, &handle);
    }
    if (rc == EV_OK) {
        rc = ev_demo_app_publish_owned(app, &msg);
    } else {
        ++app->stats.publish_errors;
        (void)ev_msg_dispose(&msg);
    }

    (void)ev_lease_pool_release(&handle);
    return rc;
}



typedef struct {
    size_t irq_samples;
    size_t net_samples;
    size_t pump_calls;
    size_t turns;
    size_t messages;
} ev_demo_app_poll_diag_t;

typedef struct {
    size_t pump_calls_used;
    size_t messages_used;
    size_t turns_used;
    size_t irq_samples_used;
    size_t net_samples_used;
    bool exhausted;
} ev_poll_budget_t;

static void ev_demo_app_poll_diag_reset(ev_demo_app_poll_diag_t *diag)
{
    if (diag != NULL) {
        memset(diag, 0, sizeof(*diag));
    }
}

static void ev_demo_app_record_poll_diag(ev_demo_app_t *app,
                                         const ev_demo_app_poll_diag_t *diag,
                                         size_t pending_before,
                                         size_t pending_after,
                                         uint32_t elapsed_ms)
{
    if ((app == NULL) || (diag == NULL)) {
        return;
    }

    app->stats.irq_samples_drained += (uint32_t)diag->irq_samples;
    if (pending_before > app->stats.max_pending_before_poll) {
        app->stats.max_pending_before_poll = pending_before;
    }
    if (pending_after > app->stats.max_pending_after_poll) {
        app->stats.max_pending_after_poll = pending_after;
    }
    if (diag->irq_samples > app->stats.max_irq_samples_per_poll) {
        app->stats.max_irq_samples_per_poll = diag->irq_samples;
    }
    if (diag->net_samples > app->stats.max_net_samples_per_poll) {
        app->stats.max_net_samples_per_poll = diag->net_samples;
    }
    if (diag->pump_calls > app->stats.max_pump_calls_per_poll) {
        app->stats.max_pump_calls_per_poll = diag->pump_calls;
    }
    if (diag->turns > app->stats.max_turns_per_poll) {
        app->stats.max_turns_per_poll = diag->turns;
    }
    if (diag->messages > app->stats.max_messages_per_poll) {
        app->stats.max_messages_per_poll = diag->messages;
    }
    app->stats.last_poll_elapsed_ms = elapsed_ms;
    if (elapsed_ms > app->stats.max_poll_elapsed_ms) {
        app->stats.max_poll_elapsed_ms = elapsed_ms;
    }
}

static void ev_demo_app_record_irq_stats(ev_demo_app_t *app)
{
    ev_irq_stats_t irq_stats = {0};

    if ((app == NULL) || (app->irq_port == NULL) || (app->irq_port->get_stats == NULL)) {
        return;
    }
    if (app->irq_port->get_stats(app->irq_port->ctx, &irq_stats) != EV_OK) {
        return;
    }

    app->stats.irq_samples_dropped_observed = irq_stats.dropped_samples;
    if (irq_stats.pending_samples > app->stats.irq_samples_pending_high_watermark) {
        app->stats.irq_samples_pending_high_watermark = irq_stats.pending_samples;
    }
    if (irq_stats.high_watermark > app->stats.irq_ring_high_watermark_observed) {
        app->stats.irq_ring_high_watermark_observed = irq_stats.high_watermark;
    }
}

static void ev_demo_app_record_net_stats(ev_demo_app_t *app)
{
    ev_net_stats_t stats;

    if ((app == NULL) || (app->net_port == NULL) || (app->net_port->get_stats == NULL)) {
        return;
    }
    if (app->net_port->get_stats(app->net_port->ctx, &stats) != EV_OK) {
        return;
    }
    if (stats.dropped_events > app->stats.net_events_dropped_observed) {
        app->stats.net_events_dropped_observed = stats.dropped_events;
    }
    if (stats.dropped_oversize > app->stats.net_payload_dropped_oversize) {
        app->stats.net_payload_dropped_oversize = stats.dropped_oversize;
    }
    if (stats.dropped_no_payload_slot > app->stats.net_no_payload_slot_drops_observed) {
        app->stats.net_no_payload_slot_drops_observed = stats.dropped_no_payload_slot;
    }
    if (stats.high_watermark > app->stats.net_ring_high_watermark_observed) {
        app->stats.net_ring_high_watermark_observed = stats.high_watermark;
    }
}



static bool ev_demo_app_budget_exhausted(const ev_poll_budget_t *budget)
{
    if (budget == NULL) {
        return true;
    }

    return (budget->pump_calls_used >= EV_APP_POLL_MAX_PUMP_TURNS) ||
           (budget->messages_used >= EV_APP_POLL_MAX_MESSAGES) ||
           (budget->turns_used >= EV_APP_POLL_MAX_PUMP_TURNS) ||
           (budget->irq_samples_used >= EV_APP_POLL_MAX_IRQ_SAMPLES) ||
           (budget->net_samples_used >= EV_APP_POLL_MAX_NET_SAMPLES);
}

static ev_result_t ev_demo_app_collect_ingress(ev_demo_app_t *app,
                                               ev_poll_budget_t *budget,
                                               ev_demo_app_poll_diag_t *diag)
{
    ev_result_t rc;
    ev_irq_sample_t sample = {0};
    size_t reserved_used = 0U;

    if ((app == NULL) || (budget == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (app->sleep_arming || budget->exhausted) {
        return EV_OK;
    }

    if ((app->irq_port != NULL) && (app->irq_port->pop != NULL)) {
        while ((budget->irq_samples_used < EV_APP_POLL_MAX_IRQ_SAMPLES) &&
               !budget->exhausted &&
               (reserved_used < EV_APP_POLL_RESERVED_IRQ_SAMPLES)) {
            rc = app->irq_port->pop(app->irq_port->ctx, &sample);
            if (rc == EV_ERR_EMPTY) {
                break;
            }
            if (rc != EV_OK) {
                return rc;
            }

            ++budget->irq_samples_used;
            ++reserved_used;
            if (diag != NULL) {
                ++diag->irq_samples;
            }

            rc = ev_demo_app_publish_irq_sample(app, &sample);
            if (rc != EV_OK) {
                return rc;
            }

            budget->exhausted = ev_demo_app_budget_exhausted(budget);
        }

        if (budget->irq_samples_used >= EV_APP_POLL_MAX_IRQ_SAMPLES) {
            budget->exhausted = true;
        }
    }

    if (!app->sleep_arming && (app->net_port != NULL) && (app->net_port->poll_ingress != NULL) &&
        !budget->exhausted) {
        ev_net_ingress_event_t net_event;
        size_t net_reserved_used = 0U;

        while ((budget->net_samples_used < EV_APP_POLL_MAX_NET_SAMPLES) &&
               !budget->exhausted &&
               (net_reserved_used < EV_APP_POLL_RESERVED_NET_SAMPLES)) {
            memset(&net_event, 0, sizeof(net_event));
            rc = app->net_port->poll_ingress(app->net_port->ctx, &net_event);
            if (rc == EV_ERR_EMPTY) {
                break;
            }
            if (rc != EV_OK) {
                return rc;
            }

            ++budget->net_samples_used;
            ++net_reserved_used;
            ++app->stats.net_ingress_drained;
            if (diag != NULL) {
                ++diag->net_samples;
            }

            rc = ev_demo_app_publish_net_event(app, &net_event);
            if (rc != EV_OK) {
                return rc;
            }

            budget->exhausted = ev_demo_app_budget_exhausted(budget);
        }
    }

    if (budget->net_samples_used >= EV_APP_POLL_MAX_NET_SAMPLES) {
        budget->exhausted = true;
    }
    return EV_OK;
}


static void ev_demo_app_release_net_event_external_payload(const ev_net_ingress_event_t *event)
{
    if ((event != NULL) && (event->payload_storage == EV_NET_PAYLOAD_LEASE) &&
        (event->external_payload.release_fn != NULL) && (event->external_payload.data != NULL) &&
        (event->external_payload.size > 0U)) {
        event->external_payload.release_fn(
            event->external_payload.lifecycle_ctx,
            event->external_payload.data,
            event->external_payload.size);
    }
}

static ev_result_t ev_demo_app_publish_net_event(ev_demo_app_t *app, const ev_net_ingress_event_t *event)
{
    ev_msg_t msg = {0};
    ev_event_id_t event_id;
    ev_result_t rc;

    if ((app == NULL) || (event == NULL)) {
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
        event_id = (event->payload_storage == EV_NET_PAYLOAD_LEASE) ? EV_NET_MQTT_MSG_RX_LEASE : EV_NET_MQTT_MSG_RX;
        break;
    default:
        ev_demo_app_release_net_event_external_payload(event);
        return EV_ERR_CONTRACT;
    }

    rc = ev_msg_init_publish(&msg, event_id, ACT_RUNTIME);
    if (rc != EV_OK) {
        ev_demo_app_release_net_event_external_payload(event);
        return rc;
    }
    if (event_id == EV_NET_MQTT_MSG_RX) {
        ev_net_mqtt_inline_payload_t inline_payload;

        if (event->payload_storage != EV_NET_PAYLOAD_INLINE) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return EV_ERR_CONTRACT;
        }
        if ((event->topic_len > EV_NET_MAX_TOPIC_BYTES) ||
            (event->payload_len > EV_NET_MAX_INLINE_PAYLOAD_BYTES)) {
            (void)ev_msg_dispose(&msg);
            return EV_ERR_CONTRACT;
        }
        memset(&inline_payload, 0, sizeof(inline_payload));
        inline_payload.topic_len = event->topic_len;
        inline_payload.payload_len = event->payload_len;
        if (event->topic_len > 0U) {
            memcpy(inline_payload.topic, event->topic, event->topic_len);
        }
        if (event->payload_len > 0U) {
            memcpy(inline_payload.payload, event->payload, event->payload_len);
        }
        rc = ev_msg_set_inline_payload(&msg, &inline_payload, sizeof(inline_payload));
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            return rc;
        }
    } else if (event_id == EV_NET_MQTT_MSG_RX_LEASE) {
        if ((event->external_payload.data == NULL) ||
            (event->external_payload.size != sizeof(ev_net_mqtt_rx_payload_t)) ||
            (event->external_payload.retain_fn == NULL) ||
            (event->external_payload.release_fn == NULL)) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return EV_ERR_CONTRACT;
        }
        rc = ev_msg_set_external_payload(&msg,
                                         event->external_payload.data,
                                         event->external_payload.size,
                                         event->external_payload.retain_fn,
                                         event->external_payload.release_fn,
                                         event->external_payload.lifecycle_ctx);
        if (rc != EV_OK) {
            (void)ev_msg_dispose(&msg);
            ev_demo_app_release_net_event_external_payload(event);
            return rc;
        }
    }
    return ev_demo_app_publish_owned(app, &msg);
}


static ev_result_t ev_demo_app_timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_send(&app->graph, target_actor, msg);
}

static ev_result_t ev_demo_app_loop_now(void *ctx, uint32_t *out_now_ms)
{
    return ev_demo_app_now_ms((ev_demo_app_t *)ctx, out_now_ms);
}

static ev_result_t ev_demo_app_loop_collect(ev_runtime_graph_t *graph,
                                            void *context,
                                            ev_runtime_loop_report_t *report,
                                            const ev_runtime_loop_policy_t *policy)
{
    ev_demo_app_t *app = (ev_demo_app_t *)context;
    ev_poll_budget_t budget;
    ev_demo_app_poll_diag_t diag;
    ev_result_t rc;

    (void)graph;
    if ((app == NULL) || (report == NULL) || (policy == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    memset(&budget, 0, sizeof(budget));
    ev_demo_app_poll_diag_reset(&diag);
    budget.pump_calls_used = report->pump_calls;
    budget.messages_used = report->messages;
    budget.turns_used = report->turns;
    budget.irq_samples_used = report->irq_samples;
    budget.net_samples_used = report->net_samples;
    budget.exhausted = report->exhausted != 0U;

    rc = ev_demo_app_collect_ingress(app, &budget, &diag);
    report->irq_samples = (uint32_t)budget.irq_samples_used;
    report->net_samples = (uint32_t)budget.net_samples_used;
    report->exhausted = budget.exhausted ? 1U : 0U;
    (void)policy;
    return rc;
}

static ev_result_t ev_demo_app_loop_work_pending(ev_runtime_graph_t *graph,
                                                 void *context,
                                                 uint32_t now_ms,
                                                 const ev_runtime_loop_report_t *report,
                                                 uint8_t *out_pending)
{
    ev_demo_app_t *app = (ev_demo_app_t *)context;
    bool irq_work_pending = false;
    bool net_work_pending = false;
    bool timer_due = false;

    (void)report;
    if ((graph == NULL) || (app == NULL) || (out_pending == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((app->irq_port != NULL) && (app->irq_port->wait != NULL)) {
        (void)app->irq_port->wait(app->irq_port->ctx, 0U, &irq_work_pending);
    }
    if ((app->net_port != NULL) && (app->net_port->get_stats != NULL)) {
        ev_net_stats_t net_stats;
        if (app->net_port->get_stats(app->net_port->ctx, &net_stats) == EV_OK) {
            net_work_pending = (net_stats.pending_events > 0U);
        }
    }
    {
        ev_quiescence_report_t q = {0};
        ev_quiescence_policy_t quiescence_policy = {0};
        quiescence_policy.block_due_timers = 1U;
        if (ev_runtime_is_quiescent_at(graph, now_ms, &quiescence_policy, &q) != EV_OK) {
            timer_due = (q.due_timers > 0U);
        }
    }
    *out_pending = (irq_work_pending || net_work_pending || timer_due) ? 1U : 0U;
    return EV_OK;
}

ev_result_t ev_demo_runtime_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_demo_app_t *app = (ev_demo_app_t *)actor_context;
    if ((app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    switch (msg->event_id) {
    case EV_TICK_100MS:
        return ev_demo_app_publish_tick_100ms(app);
    case EV_TICK_1S:
        return ev_demo_app_publish_tick(app);
    default:
        return EV_OK;
    }
}

ev_result_t ev_demo_app_init(ev_demo_app_t *app, const ev_demo_app_config_t *cfg)
{
    ev_result_t rc;

    if ((app == NULL) || !ev_demo_app_config_is_valid(cfg)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(app, 0, sizeof(*app));
    app->clock_port = cfg->clock_port;
    app->log_port = cfg->log_port;
    app->irq_port = cfg->irq_port;
    app->system_port = cfg->system_port;
    app->wdt_port = cfg->wdt_port;
    app->net_port = cfg->net_port;
    app->app_tag = cfg->app_tag;
    app->board_name = cfg->board_name;
    app->tick_period_ms = (cfg->tick_period_ms == 0U) ? EV_DEMO_APP_DEFAULT_TICK_MS : cfg->tick_period_ms;
    app->board_profile = *((cfg->board_profile != NULL) ? cfg->board_profile : ev_demo_app_default_board_profile());
    app->app_actor.app = app;
    app->app_actor.direction_x = (int8_t)1;
    app->app_actor.direction_y = (int8_t)1;
    app->diag_actor.app = app;

    rc = ev_demo_app_configure_runtime(app, cfg);
    if (rc != EV_OK) {
        return rc;
    }

    ev_demo_app_logf(app, EV_LOG_INFO, "demo runtime ready board=%s tick_period_ms=%u", app->board_name, (unsigned)app->tick_period_ms);
    return EV_OK;
}

ev_result_t ev_demo_app_publish_boot(ev_demo_app_t *app)
{
    ev_msg_t msg = {0};
    ev_result_t rc;

    if (app == NULL) return EV_ERR_INVALID_ARG;
    if (app->boot_published) return EV_ERR_STATE;

    rc = ev_msg_init_publish(&msg, EV_BOOT_STARTED, ACT_BOOT);
    if (rc != EV_OK) return rc;

    rc = ev_demo_app_publish_owned(app, &msg);
    if (rc != EV_OK) return rc;

    rc = ev_msg_init_publish(&msg, EV_BOOT_COMPLETED, ACT_BOOT);
    if (rc != EV_OK) return rc;

    rc = ev_demo_app_publish_owned(app, &msg);
    if (rc != EV_OK) return rc;

    app->boot_published = true;
    return EV_OK;
}

ev_result_t ev_demo_app_poll(ev_demo_app_t *app)
{
    ev_runtime_loop_policy_t policy;
    ev_runtime_loop_ports_t ports;
    ev_runtime_loop_report_t report;
    ev_demo_app_poll_diag_t diag;
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!app->boot_published) {
        return EV_ERR_STATE;
    }

    ev_runtime_loop_policy_default(&policy);
    policy.max_pump_calls = EV_APP_POLL_MAX_PUMP_TURNS;
    policy.max_messages = EV_APP_POLL_MAX_MESSAGES;
    policy.max_turns = EV_APP_POLL_MAX_PUMP_TURNS;
    policy.max_irq_samples = EV_APP_POLL_MAX_IRQ_SAMPLES;
    policy.max_net_samples = EV_APP_POLL_MAX_NET_SAMPLES;
    policy.timer_publish_budget = 1U;
    policy.scheduler_turn_budget = EV_DEMO_APP_TURN_BUDGET;
    policy.skip_timers_when_scheduler_pending = 1U;
    policy.run_scheduler_after_timers = 1U;
    policy.skip_timers = app->sleep_arming ? 1U : 0U;

    memset(&ports, 0, sizeof(ports));
    ports.collect_ingress = ev_demo_app_loop_collect;
    ports.collect_ctx = app;
    ports.timer_delivery = ev_demo_app_timer_delivery;
    ports.timer_delivery_ctx = app;
    ports.now_ms = ev_demo_app_loop_now;
    ports.now_ctx = app;
    ports.work_pending = ev_demo_app_loop_work_pending;
    ports.work_pending_ctx = app;

    rc = ev_runtime_loop_poll_once(&app->graph, &policy, &ports, &report);

    ev_demo_app_poll_diag_reset(&diag);
    diag.irq_samples = report.irq_samples;
    diag.net_samples = report.net_samples;
    diag.pump_calls = report.pump_calls;
    diag.turns = report.turns;
    diag.messages = report.messages;
    ev_demo_app_record_poll_diag(app, &diag, report.pending_before, report.pending_after, report.elapsed_ms);
    ev_demo_app_record_irq_stats(app);
    ev_demo_app_record_net_stats(app);
    ev_demo_app_record_publish_port_stats(app);
    return rc;
}

size_t ev_demo_app_pending(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_runtime_graph_scheduler_pending(&app->graph) : 0U;
}


ev_result_t ev_demo_app_next_deadline_ms(const ev_demo_app_t *app, uint32_t *out_deadline_ms)
{
    if ((app == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_next_deadline_ms(&app->graph, out_deadline_ms);
}

ev_result_t ev_demo_app_post_event(ev_demo_app_t *app,
                                   ev_event_id_t event_id,
                                   ev_actor_id_t source_actor,
                                   const void *payload,
                                   size_t payload_size)
{
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_graph_post_event(&app->graph, event_id, source_actor, payload, payload_size);
}

const ev_demo_app_stats_t *ev_demo_app_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? &app->stats : NULL;
}

const ev_system_pump_stats_t *ev_demo_app_system_pump_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_runtime_graph_system_pump_stats(&app->graph) : NULL;
}

const ev_watchdog_actor_stats_t *ev_demo_app_watchdog_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_watchdog_actor_stats(&app->watchdog_ctx) : NULL;
}

const ev_network_actor_stats_t *ev_demo_app_network_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_network_actor_stats(&app->network_ctx) : NULL;
}

const ev_command_actor_stats_t *ev_demo_app_command_stats(const ev_demo_app_t *app)
{
    return (app != NULL) ? ev_command_actor_stats(&app->command_ctx) : NULL;
}
