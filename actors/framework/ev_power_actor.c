#include "ev/power_actor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define EV_POWER_US_PER_MS 1000ULL

static void ev_power_actor_set_reject_reason(ev_power_actor_ctx_t *ctx,
                                             ev_power_sleep_reject_reason_t reason)
{
    if (ctx != NULL) {
        ctx->last_reject_reason = reason;
    }
}

static void ev_power_actor_reject(ev_power_actor_ctx_t *ctx, ev_power_sleep_reject_reason_t reason)
{
    if (ctx == NULL) {
        return;
    }

    ++ctx->sleep_requests_rejected;
    ev_power_actor_set_reject_reason(ctx, reason);
}

static ev_result_t ev_power_actor_log(ev_power_actor_ctx_t *ctx, const char *message)
{
    const char *tag;

    if ((ctx == NULL) || (message == NULL) || (ctx->log_port == NULL) ||
        (ctx->log_port->write == NULL)) {
        return EV_OK;
    }

    tag = (ctx->log_tag != NULL) ? ctx->log_tag : "ev_power";
    return ctx->log_port->write(ctx->log_port->ctx, EV_LOG_INFO, tag, message, strlen(message));
}

static ev_result_t ev_power_actor_flush_log(ev_power_actor_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->log_port == NULL) || (ctx->log_port->flush == NULL)) {
        return EV_OK;
    }

    return ctx->log_port->flush(ctx->log_port->ctx);
}

static ev_result_t ev_power_actor_run_quiescence_guard(ev_power_actor_ctx_t *ctx, uint64_t duration_us)
{
    ev_power_quiescence_report_t report;
    ev_result_t rc;

    if ((ctx == NULL) || (ctx->quiescence_guard == NULL)) {
        return EV_OK;
    }

    memset(&report, 0, sizeof(report));
    report.reason = EV_POWER_SLEEP_REJECT_NONE;
    rc = ctx->quiescence_guard(ctx->quiescence_guard_ctx, duration_us, &report);
    ctx->last_quiescence_report = report;
    if (rc != EV_OK) {
        ++ctx->sleep_quiescence_failures;
        ev_power_actor_reject(ctx,
                              (report.reason != EV_POWER_SLEEP_REJECT_NONE) ?
                                  report.reason :
                                  EV_POWER_SLEEP_REJECT_NOT_QUIESCENT);
        return rc;
    }

    return EV_OK;
}

static ev_result_t ev_power_actor_arm_sleep(ev_power_actor_ctx_t *ctx, uint64_t duration_us, bool *out_armed)
{
    ev_power_quiescence_report_t report;
    ev_result_t rc;

    if (out_armed == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    *out_armed = false;

    if ((ctx == NULL) || (ctx->sleep_arm == NULL)) {
        return ev_power_actor_run_quiescence_guard(ctx, duration_us);
    }

    memset(&report, 0, sizeof(report));
    report.reason = EV_POWER_SLEEP_REJECT_NONE;
    rc = ctx->sleep_arm(ctx->sleep_arming_ctx, duration_us, &report);
    ctx->last_quiescence_report = report;
    if (rc != EV_OK) {
        ++ctx->sleep_arming_failures;
        if ((report.pending_actor_messages != 0U) || (report.pending_irq_samples != 0U) ||
            (report.due_timer_count != 0U) || (report.pending_oled_flush != 0U) ||
            (report.pending_ds18b20_conversion != 0U)) {
            ++ctx->sleep_quiescence_failures;
        }
        ev_power_actor_reject(ctx,
                              (report.reason != EV_POWER_SLEEP_REJECT_NONE) ?
                                  report.reason :
                                  EV_POWER_SLEEP_REJECT_ARMING_FAILED);
        return rc;
    }

    *out_armed = true;
    return EV_OK;
}

static ev_result_t ev_power_actor_disarm_sleep(ev_power_actor_ctx_t *ctx)
{
    ev_result_t rc = EV_OK;

    if ((ctx == NULL) || (ctx->sleep_disarm == NULL)) {
        return EV_OK;
    }

    ++ctx->sleep_disarm_calls;
    rc = ctx->sleep_disarm(ctx->sleep_arming_ctx);
    if (rc != EV_OK) {
        ++ctx->sleep_disarm_failures;
    }
    return rc;
}

static ev_result_t ev_power_actor_cancel_platform_sleep_prepare(ev_power_actor_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->system_port == NULL) ||
        (ctx->system_port->cancel_sleep_prepare == NULL)) {
        return EV_OK;
    }

    return ctx->system_port->cancel_sleep_prepare(ctx->system_port->ctx);
}


ev_result_t ev_power_actor_init(ev_power_actor_ctx_t *ctx,
                                ev_system_port_t *system_port,
                                ev_log_port_t *log_port,
                                const char *log_tag)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->system_port = system_port;
    ctx->log_port = log_port;
    ctx->log_tag = log_tag;
    ctx->last_reject_reason = EV_POWER_SLEEP_REJECT_NONE;
    return EV_OK;
}

ev_result_t ev_power_actor_set_quiescence_guard(ev_power_actor_ctx_t *ctx,
                                                ev_power_quiescence_guard_fn_t guard,
                                                void *guard_ctx)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ctx->quiescence_guard = guard;
    ctx->quiescence_guard_ctx = guard_ctx;
    return EV_OK;
}

ev_result_t ev_power_actor_set_sleep_arming(ev_power_actor_ctx_t *ctx,
                                            ev_power_sleep_arm_fn_t arm,
                                            ev_power_sleep_disarm_fn_t disarm,
                                            void *arming_ctx)
{
    if (ctx == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ctx->sleep_arm = arm;
    ctx->sleep_disarm = disarm;
    ctx->sleep_arming_ctx = arming_ctx;
    return EV_OK;
}

ev_result_t ev_power_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_power_actor_ctx_t *ctx = (ev_power_actor_ctx_t *)actor_context;
    const ev_sys_goto_sleep_cmd_t *payload;
    uint64_t duration_us;
    ev_result_t rc;
    bool sleep_armed = false;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    switch (msg->event_id) {
    case EV_SYS_GOTO_SLEEP_CMD:
        payload = (const ev_sys_goto_sleep_cmd_t *)ev_msg_payload_data(msg);
        if ((payload == NULL) || (ev_msg_payload_size(msg) != sizeof(*payload))) {
            ++ctx->bad_payload_failures;
            ev_power_actor_reject(ctx, EV_POWER_SLEEP_REJECT_BAD_PAYLOAD);
            return EV_ERR_CONTRACT;
        }
        if ((payload->duration_ms == 0U) || (payload->duration_ms > EV_POWER_MAX_SLEEP_DURATION_MS)) {
            ++ctx->bad_duration_failures;
            ev_power_actor_reject(ctx, EV_POWER_SLEEP_REJECT_BAD_DURATION);
            return EV_ERR_OUT_OF_RANGE;
        }

        ++ctx->sleep_requests_seen;
        duration_us = (uint64_t)payload->duration_ms * EV_POWER_US_PER_MS;
        ctx->last_duration_us = duration_us;
        ctx->last_reject_reason = EV_POWER_SLEEP_REJECT_NONE;
        memset(&ctx->last_quiescence_report, 0, sizeof(ctx->last_quiescence_report));

        if ((ctx->system_port == NULL) || (ctx->system_port->deep_sleep == NULL)) {
            ++ctx->sleep_requests_unsupported;
            ev_power_actor_set_reject_reason(ctx, EV_POWER_SLEEP_REJECT_UNSUPPORTED);
            return EV_OK;
        }

        rc = ev_power_actor_arm_sleep(ctx, duration_us, &sleep_armed);
        if (rc != EV_OK) {
            return rc;
        }

        (void)ev_power_actor_log(ctx, "System entering Deep Sleep");
        rc = ev_power_actor_flush_log(ctx);
        if (rc != EV_OK) {
            ++ctx->log_flush_failures;
            ev_power_actor_reject(ctx, EV_POWER_SLEEP_REJECT_LOG_FLUSH_FAILED);
            if (sleep_armed) {
                (void)ev_power_actor_disarm_sleep(ctx);
            }
            return rc;
        }

        if (ctx->system_port->prepare_for_sleep != NULL) {
            rc = ctx->system_port->prepare_for_sleep(ctx->system_port->ctx, duration_us);
            if (rc != EV_OK) {
                ++ctx->prepare_for_sleep_failures;
                ev_power_actor_reject(ctx, EV_POWER_SLEEP_REJECT_PREPARE_FAILED);
                if (sleep_armed) {
                    (void)ev_power_actor_disarm_sleep(ctx);
                }
                return rc;
            }
        }

        ++ctx->sleep_requests_accepted;
        rc = ctx->system_port->deep_sleep(ctx->system_port->ctx, duration_us);
        if (rc != EV_OK) {
            ++ctx->deep_sleep_failures;
            ev_power_actor_set_reject_reason(ctx, EV_POWER_SLEEP_REJECT_DEEP_SLEEP_FAILED);
            (void)ev_power_actor_cancel_platform_sleep_prepare(ctx);
            if (sleep_armed) {
                (void)ev_power_actor_disarm_sleep(ctx);
            }
            return rc;
        }
        if (sleep_armed) {
            (void)ev_power_actor_disarm_sleep(ctx);
        }
        return EV_OK;

    default:
        return EV_ERR_CONTRACT;
    }
}
