#include "ev/demo_policy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ev/compiler.h"
#include "ev/demo_internal.h"
#include "ev/demo_presentation.h"
#include "ev/dispose.h"
#include "ev/msg.h"
#include "ev/runtime_graph_inspection.h"

#define EV_DEMO_APP_BUTTON_TOGGLE_SCREENSAVER 0U
#define EV_DEMO_APP_LED_SCREENSAVER_PAUSED 0x01U

typedef struct {
    uint32_t sequence;
    uint32_t ticks_seen;
    uint32_t last_tick_ms;
    uint32_t boot_completions;
} ev_demo_snapshot_t;

EV_STATIC_ASSERT(sizeof(ev_demo_snapshot_t) == EV_DEMO_APP_SNAPSHOT_BYTES, "demo snapshot ABI mismatch");
EV_STATIC_ASSERT(sizeof(ev_oled_scene_t) <= EV_DEMO_APP_LEASE_SLOT_BYTES,
                 "OLED scene payload must fit inside one demo lease slot");

static ev_result_t ev_demo_app_publish_snapshot(ev_demo_diag_actor_state_t *state)
{
    ev_demo_app_t *app;
    ev_lease_handle_t handle = {0};
    ev_demo_snapshot_t *snapshot = NULL;
    ev_msg_t msg = {0};
    void *data = NULL;
    ev_result_t rc;

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    rc = ev_lease_pool_acquire(&app->lease_pool, sizeof(*snapshot), &handle, &data);
    if (rc != EV_OK) {
        ++app->stats.publish_errors;
        ev_demo_app_logf(app, EV_LOG_ERROR, "snapshot acquire failed rc=%d", (int)rc);
        return rc;
    }

    snapshot = (ev_demo_snapshot_t *)data;
    snapshot->sequence = state->snapshots_sent + 1U;
    snapshot->ticks_seen = state->ticks_seen;
    snapshot->last_tick_ms = state->last_tick_ms;
    snapshot->boot_completions = app->stats.boot_completions;

    rc = ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_RSP, ACT_DIAG);
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
    if (rc != EV_OK) {
        return rc;
    }

    ++state->snapshots_sent;
    ++app->stats.snapshots_published;
    return EV_OK;
}

static ev_result_t ev_demo_app_publish_diag_request(ev_demo_app_actor_state_t *state)
{
    ev_demo_app_t *app;
    ev_msg_t msg = {0};
    ev_result_t rc;

    if ((state == NULL) || (state->app == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;
    rc = ev_msg_init_publish(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP);
    if (rc != EV_OK) {
        return rc;
    }

    return ev_demo_app_publish_owned(app, &msg);
}

ev_result_t ev_demo_app_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    const ev_demo_snapshot_t *snapshot;
    ev_demo_app_actor_state_t *state = (ev_demo_app_actor_state_t *)actor_context;
    ev_demo_app_t *app;

    if ((state == NULL) || (state->app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;

    switch (msg->event_id) {
    case EV_BOOT_COMPLETED:
        ++app->stats.boot_completions;
        state->current_page_offset = 0U;
        state->current_column_offset = 0U;
        state->last_page_offset = 0U;
        state->last_column_offset = 0U;
        state->direction_x = (int8_t)1;
        state->direction_y = (int8_t)1;
        state->oled_frame_visible = false;
        state->screensaver_paused = false;
        state->panel_led_mask = 0U;
        state->system_ready = false;
        state->light_valid = false;
        state->active_hardware_mask = 0U;
        return EV_OK;

    case EV_SYSTEM_READY:
        {
            const ev_system_ready_payload_t *ready_payload = (const ev_system_ready_payload_t *)ev_msg_payload_data(msg);
            ev_result_t rc;
            bool first_system_ready;

            if ((ready_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*ready_payload))) {
                return EV_ERR_CONTRACT;
            }

            first_system_ready = !state->system_ready;
            state->system_ready = true;
            state->active_hardware_mask = ready_payload->active_hardware_mask;
            state->temp_valid = (state->temp_valid && ((state->active_hardware_mask & EV_SUPERVISOR_HW_DS18B20) != 0U));
            state->light_valid = (state->light_valid && ((state->active_hardware_mask & EV_SUPERVISOR_HW_BH1750) != 0U));
            ev_demo_app_logf(app, EV_LOG_INFO, "app actor: system ready hw_mask=0x%08lx", (unsigned long)state->active_hardware_mask);

            if (first_system_ready) {
                if ((state->active_hardware_mask & EV_SUPERVISOR_HW_MCP23008) != 0U) {
                    rc = ev_demo_app_publish_panel_led_command(app, 0U, EV_MCP23008_LED_MASK);
                    if (rc != EV_OK) {
                        return rc;
                    }
                }

                rc = ev_demo_app_publish_diag_request(state);
                if (rc != EV_OK) {
                    return rc;
                }
            }

            return ev_demo_app_render_oled_frame(state);
        }

    case EV_TICK_1S:
        if (!state->system_ready) {
            return EV_OK;
        }
        return ev_demo_app_handle_tick_for_oled(state);

    case EV_TEMP_UPDATED:
        {
            const ev_temp_payload_t *temp_payload = (const ev_temp_payload_t *)ev_msg_payload_data(msg);

            if ((temp_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*temp_payload))) {
                return EV_ERR_CONTRACT;
            }

            state->last_temp = *temp_payload;
            state->temp_valid = true;
            return EV_OK;
        }

    case EV_LIGHT_UPDATED:
        {
            const ev_light_payload_t *light_payload = (const ev_light_payload_t *)ev_msg_payload_data(msg);

            if ((light_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*light_payload))) {
                return EV_ERR_CONTRACT;
            }

            state->last_light = *light_payload;
            state->light_valid = true;
            return EV_OK;
        }

    case EV_TIME_UPDATED:
        {
            const ev_time_payload_t *time_payload = (const ev_time_payload_t *)ev_msg_payload_data(msg);

            if ((time_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*time_payload))) {
                return EV_ERR_CONTRACT;
            }

            state->last_time = *time_payload;
            state->time_valid = true;
            return EV_OK;
        }

    case EV_BUTTON_EVENT:
        {
            const ev_button_event_payload_t *button_payload =
                (const ev_button_event_payload_t *)ev_msg_payload_data(msg);

            if ((button_payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*button_payload))) {
                return EV_ERR_CONTRACT;
            }

            if ((button_payload->button_id == EV_DEMO_APP_BUTTON_TOGGLE_SCREENSAVER) &&
                (button_payload->action == EV_BUTTON_ACTION_SHORT)) {
                ev_result_t rc;

                state->screensaver_paused = !state->screensaver_paused;
                if (state->screensaver_paused) {
                    state->panel_led_mask = (uint8_t)(state->panel_led_mask | EV_DEMO_APP_LED_SCREENSAVER_PAUSED);
                } else {
                    state->panel_led_mask =
                        (uint8_t)(state->panel_led_mask & (uint8_t)(~EV_DEMO_APP_LED_SCREENSAVER_PAUSED));
                }

                rc = ev_demo_app_publish_panel_led_command(app,
                                                           state->panel_led_mask,
                                                           EV_DEMO_APP_LED_SCREENSAVER_PAUSED);
                if (rc != EV_OK) {
                    return rc;
                }

                ev_demo_app_logf(app,
                                 EV_LOG_INFO,
                                 "app actor: screensaver %s",
                                 state->screensaver_paused ? "paused" : "resumed");
            }

            return EV_OK;
        }

    case EV_DIAG_SNAPSHOT_RSP:
        snapshot = (const ev_demo_snapshot_t *)ev_msg_payload_data(msg);
        if ((snapshot == NULL) || (ev_msg_payload_size(msg) != sizeof(*snapshot))) {
            return EV_ERR_CONTRACT;
        }
        state->last_snapshot_sequence = snapshot->sequence;
        state->last_diag_ticks_seen = snapshot->ticks_seen;
        ++app->stats.snapshots_received;
        ev_demo_app_logf(app,
                         EV_LOG_INFO,
                         "app actor: snapshot seq=%u diag_ticks=%u last_tick_ms=%u",
                         (unsigned)snapshot->sequence,
                         (unsigned)snapshot->ticks_seen,
                         (unsigned)snapshot->last_tick_ms);
        return EV_OK;

    default:
        return EV_ERR_CONTRACT;
    }
}

ev_result_t ev_demo_diag_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_demo_diag_actor_state_t *state = (ev_demo_diag_actor_state_t *)actor_context;
    ev_demo_app_t *app;
    ev_result_t rc;
    uint32_t now_ms = 0U;

    if ((state == NULL) || (state->app == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    app = state->app;

    switch (msg->event_id) {
    case EV_BOOT_STARTED:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: observed boot start on %s", app->board_name);
        return EV_OK;

    case EV_BOOT_COMPLETED:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: observed boot completion");
        return EV_OK;

    case EV_TICK_1S:
        rc = ev_demo_app_now_ms(app, &now_ms);
        if (rc != EV_OK) {
            return rc;
        }
        state->last_tick_ms = now_ms;
        ++state->ticks_seen;
        ++app->stats.diag_ticks_seen;
        ev_demo_app_logf(app,
                         EV_LOG_INFO,
                         "diag actor: tick=%u mono_now_ms=%u",
                         (unsigned)state->ticks_seen,
                         (unsigned)state->last_tick_ms);


        return ev_demo_app_publish_snapshot(state);

    case EV_TICK_100MS:
        return EV_OK;

    case EV_MCP23008_READY:
        ev_demo_app_logf(app, EV_LOG_INFO, "diag actor: mcp23008 ready -> rtc may arm sqw irq");
        return EV_OK;

    case EV_GPIO_IRQ:
        {
            const ev_irq_sample_t *sample = (const ev_irq_sample_t *)ev_msg_payload_data(msg);
            const char *edge_name;

            if ((sample == NULL) || (ev_msg_payload_size(msg) != sizeof(*sample))) {
                return EV_ERR_CONTRACT;
            }

            if (sample->line_id == app->board_profile.rtc_sqw_line_id) {
                edge_name = (sample->edge == EV_IRQ_EDGE_FALLING) ? "falling" :
                            ((sample->edge == EV_IRQ_EDGE_RISING) ? "rising" : "unknown");
                ++state->rtc_irq_samples_seen;
                ev_demo_app_logf(app,
                                 EV_LOG_INFO,
                                 "diag actor: rtc irq count=%u line=%u edge=%s level=%u ts_us=%lu",
                                 (unsigned)state->rtc_irq_samples_seen,
                                 (unsigned)sample->line_id,
                                 edge_name,
                                 (unsigned)sample->level,
                                 (unsigned long)sample->timestamp_us);
            }

            return EV_OK;
        }

    case EV_DIAG_SNAPSHOT_REQ:
        return ev_demo_app_publish_snapshot(state);

    default:
        return EV_ERR_CONTRACT;
    }
}

ev_result_t ev_demo_app_sleep_quiescence_guard(void *ctx,
                                                       uint64_t duration_us,
                                                       ev_power_quiescence_report_t *out_report)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    ev_power_quiescence_report_t report;
    ev_quiescence_report_t qreport;
    ev_quiescence_policy_t policy;
    bool irq_pending = false;
    uint32_t now_ms = 0U;
    ev_result_t rc;

    (void)duration_us;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    memset(&report, 0, sizeof(report));
    memset(&policy, 0, sizeof(policy));
    policy.trace_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.fault_policy = EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY;
    policy.log_policy = EV_QUIESCENCE_BUFFER_BLOCK_NEVER;
    policy.block_due_timers = 1U;
    policy.block_actor_sleep_blockers = 1U;

    if ((app->irq_port != NULL) && (app->irq_port->get_stats != NULL)) {
        ev_irq_stats_t irq_stats = {0};
        rc = app->irq_port->get_stats(app->irq_port->ctx, &irq_stats);
        if (rc != EV_OK) {
            if (out_report != NULL) *out_report = report;
            return rc;
        }
        report.pending_irq_samples = irq_stats.pending_samples;
    } else if ((app->irq_port != NULL) && (app->irq_port->wait != NULL)) {
        rc = app->irq_port->wait(app->irq_port->ctx, 0U, &irq_pending);
        if (rc != EV_OK) {
            if (out_report != NULL) *out_report = report;
            return rc;
        }
        report.pending_irq_samples = irq_pending ? 1U : 0U;
    }

    rc = ev_demo_app_now_ms(app, &now_ms);
    if (rc != EV_OK) {
        if (out_report != NULL) *out_report = report;
        return rc;
    }
    rc = ev_runtime_is_quiescent_at(&app->graph, now_ms, &policy, &qreport);
    report.pending_actor_messages = qreport.pending_actor_messages;
    report.pending_oled_flush = ((qreport.sleep_blocker_actor_mask & (1UL << ACT_OLED)) != 0U) ? 1U : 0U;
    report.pending_ds18b20_conversion = ((qreport.sleep_blocker_actor_mask & (1UL << ACT_DS18B20)) != 0U) ? 1U : 0U;
    report.due_timer_count = qreport.due_timers;
    if ((rc != EV_OK) || (report.pending_irq_samples != 0U)) {
        report.reason = EV_POWER_SLEEP_REJECT_NOT_QUIESCENT;
        if (out_report != NULL) *out_report = report;
        return EV_ERR_STATE;
    }
    if (out_report != NULL) {
        *out_report = report;
    }
    return EV_OK;
}

ev_result_t ev_demo_app_sleep_arm(void *ctx,
                                         uint64_t duration_us,
                                         ev_power_quiescence_report_t *out_report)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)duration_us;
    ++app->stats.sleep_arm_attempts;
    if (app->sleep_arming) {
        ++app->stats.sleep_arm_failures;
        if (out_report != NULL) {
            memset(out_report, 0, sizeof(*out_report));
            out_report->reason = EV_POWER_SLEEP_REJECT_ARMING_FAILED;
        }
        return EV_ERR_STATE;
    }

    app->sleep_arming = true;
    rc = ev_demo_app_sleep_quiescence_guard(app, duration_us, out_report);
    if (rc != EV_OK) {
        app->sleep_arming = false;
        ++app->stats.sleep_arm_failures;
        return rc;
    }

    ++app->stats.sleep_arm_successes;
    return EV_OK;
}

ev_result_t ev_demo_app_sleep_disarm(void *ctx)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++app->stats.sleep_disarm_calls;
    app->sleep_arming = false;
    return EV_OK;
}

static void ev_demo_app_fill_watchdog_domain_snapshot(const ev_runtime_graph_t *graph,
                                                        ev_execution_domain_t domain,
                                                        ev_watchdog_domain_snapshot_t *out_snapshot)
{
    const ev_domain_pump_stats_t *stats;

    if (out_snapshot == NULL) {
        return;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (graph == NULL) {
        out_snapshot->domain = EV_DOMAIN_COUNT;
        out_snapshot->last_result = EV_ERR_STATE;
        return;
    }

    stats = ev_runtime_graph_domain_pump_stats(graph, domain);
    out_snapshot->domain = domain;
    out_snapshot->bound = (stats != NULL);
    out_snapshot->pending_messages = ev_runtime_graph_domain_pending(graph, domain);
    if (stats != NULL) {
        out_snapshot->pump_calls = stats->pump_calls;
        out_snapshot->pump_empty_calls = stats->pump_empty_calls;
        out_snapshot->pump_budget_hits = stats->pump_budget_hits;
        out_snapshot->last_result = stats->last_result;
    } else {
        out_snapshot->last_result = EV_ERR_STATE;
    }
}

ev_result_t ev_demo_app_watchdog_liveness(void *ctx, ev_watchdog_liveness_snapshot_t *out_snapshot)
{
    ev_demo_app_t *app = (ev_demo_app_t *)ctx;
    const ev_system_pump_stats_t *system_stats;

    if ((app == NULL) || (out_snapshot == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));
    system_stats = ev_runtime_graph_system_pump_stats(&app->graph);
    if (system_stats == NULL) {
        return EV_ERR_STATE;
    }

    out_snapshot->system_turn_counter = system_stats->turns_processed;
    out_snapshot->system_messages_processed = system_stats->messages_processed;
    out_snapshot->system_pending_messages = ev_runtime_graph_scheduler_pending(&app->graph);
    out_snapshot->sleep_arming = app->sleep_arming;
    out_snapshot->permanent_stall = (system_stats->last_result != EV_OK) &&
                                    (system_stats->last_result != EV_ERR_EMPTY) &&
                                    (system_stats->last_result != EV_ERR_PARTIAL);
    out_snapshot->domain_count = 2U;
    ev_demo_app_fill_watchdog_domain_snapshot(&app->graph, EV_DOMAIN_FAST_LOOP, &out_snapshot->domains[0]);
    ev_demo_app_fill_watchdog_domain_snapshot(&app->graph, EV_DOMAIN_SLOW_IO, &out_snapshot->domains[1]);
    return EV_OK;
}
