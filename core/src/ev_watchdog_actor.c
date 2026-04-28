#include "ev/watchdog_actor.h"

#include <string.h>

static bool ev_watchdog_timeout_is_valid(uint32_t timeout_ms)
{
    return (timeout_ms >= EV_WATCHDOG_MIN_TIMEOUT_MS) &&
           (timeout_ms <= EV_WATCHDOG_MAX_TIMEOUT_MS);
}

static bool ev_watchdog_domain_result_is_healthy(ev_result_t result)
{
    return (result == EV_OK) || (result == EV_ERR_EMPTY) || (result == EV_ERR_PARTIAL);
}

static bool ev_watchdog_domain_progressed(const ev_watchdog_domain_snapshot_t *now,
                                          const ev_watchdog_domain_snapshot_t *last)
{
    if ((now == NULL) || (last == NULL)) {
        return false;
    }
    return (now->pump_calls != last->pump_calls) ||
           (now->pump_empty_calls != last->pump_empty_calls) ||
           (now->pump_budget_hits != last->pump_budget_hits);
}

static const ev_watchdog_domain_snapshot_t *ev_watchdog_find_last_domain(
    const ev_watchdog_liveness_snapshot_t *snapshot,
    ev_execution_domain_t domain)
{
    size_t i;

    if (snapshot == NULL) {
        return NULL;
    }
    for (i = 0U; (i < snapshot->domain_count) && (i < EV_WATCHDOG_MAX_DOMAIN_SNAPSHOTS); ++i) {
        if (snapshot->domains[i].bound && (snapshot->domains[i].domain == domain)) {
            return &snapshot->domains[i];
        }
    }
    return NULL;
}

static bool ev_watchdog_snapshot_is_healthy(ev_watchdog_actor_ctx_t *ctx,
                                            const ev_watchdog_liveness_snapshot_t *snapshot,
                                            ev_watchdog_reject_reason_t *out_reason)
{
    size_t i;

    if ((ctx == NULL) || (snapshot == NULL) || (out_reason == NULL)) {
        return false;
    }
    if (snapshot->sleep_arming) {
        *out_reason = EV_WATCHDOG_REJECT_SLEEP_ARMING;
        return false;
    }
    if (snapshot->permanent_stall) {
        *out_reason = EV_WATCHDOG_REJECT_PERMANENT_STALL;
        return false;
    }
    if (snapshot->domain_count > EV_WATCHDOG_MAX_DOMAIN_SNAPSHOTS) {
        *out_reason = EV_WATCHDOG_REJECT_DOMAIN_STALLED;
        return false;
    }
    if (snapshot->system_turn_counter == 0U) {
        *out_reason = EV_WATCHDOG_REJECT_NO_SYSTEM_TURN;
        return false;
    }
    if (ctx->has_last_snapshot &&
        (snapshot->system_turn_counter <= ctx->last_snapshot.system_turn_counter)) {
        *out_reason = EV_WATCHDOG_REJECT_NOT_PROGRESSING;
        return false;
    }

    for (i = 0U; i < snapshot->domain_count; ++i) {
        const ev_watchdog_domain_snapshot_t *domain = &snapshot->domains[i];
        const ev_watchdog_domain_snapshot_t *last_domain;

        if (!domain->bound) {
            continue;
        }
        if (!ev_watchdog_domain_result_is_healthy(domain->last_result)) {
            *out_reason = EV_WATCHDOG_REJECT_DOMAIN_STALLED;
            return false;
        }
        if (!ctx->has_last_snapshot || (domain->pending_messages == 0U)) {
            continue;
        }

        last_domain = ev_watchdog_find_last_domain(&ctx->last_snapshot, domain->domain);
        if ((last_domain == NULL) || !ev_watchdog_domain_progressed(domain, last_domain)) {
            *out_reason = EV_WATCHDOG_REJECT_DOMAIN_STALLED;
            return false;
        }
    }

    *out_reason = EV_WATCHDOG_REJECT_NONE;
    return true;
}

ev_result_t ev_watchdog_actor_init(
    ev_watchdog_actor_ctx_t *ctx,
    ev_wdt_port_t *wdt_port,
    uint32_t timeout_ms,
    ev_watchdog_liveness_fn_t liveness_fn,
    void *liveness_ctx)
{
    bool supported = true;
    ev_result_t rc;

    if ((ctx == NULL) || (wdt_port == NULL) || (wdt_port->enable == NULL) || (wdt_port->feed == NULL) ||
        !ev_watchdog_timeout_is_valid(timeout_ms)) {
        return EV_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->wdt_port = wdt_port;
    ctx->liveness_fn = liveness_fn;
    ctx->liveness_ctx = liveness_ctx;
    ctx->timeout_ms = timeout_ms;
    ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_NONE;
    ctx->stats.last_reset_reason = EV_RESET_REASON_UNKNOWN;

    if (wdt_port->is_supported != NULL) {
        rc = wdt_port->is_supported(wdt_port->ctx, &supported);
        if (rc != EV_OK) {
            ++ctx->stats.unsupported_rejects;
            ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_UNSUPPORTED;
            return rc;
        }
    }
    ctx->supported = supported;
    if (!supported) {
        ++ctx->stats.unsupported_rejects;
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_UNSUPPORTED;
        return EV_ERR_UNSUPPORTED;
    }

    ++ctx->stats.enable_calls;
    rc = wdt_port->enable(wdt_port->ctx, timeout_ms);
    if (rc != EV_OK) {
        ++ctx->stats.enable_failures;
        ctx->stats.last_reject_reason = (rc == EV_ERR_UNSUPPORTED) ?
            EV_WATCHDOG_REJECT_UNSUPPORTED : EV_WATCHDOG_REJECT_FEED_FAILED;
        return rc;
    }

    if (wdt_port->get_reset_reason != NULL) {
        (void)wdt_port->get_reset_reason(wdt_port->ctx, &ctx->stats.last_reset_reason);
    }

    ctx->enabled = true;
    return EV_OK;
}

ev_result_t ev_watchdog_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_watchdog_actor_ctx_t *ctx = (ev_watchdog_actor_ctx_t *)actor_context;
    ev_watchdog_liveness_snapshot_t snapshot;
    ev_watchdog_reject_reason_t reject_reason = EV_WATCHDOG_REJECT_NONE;
    ev_result_t rc;

    if ((ctx == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (msg->event_id != EV_TICK_1S) {
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_BAD_EVENT;
        return EV_OK;
    }

    ++ctx->stats.ticks_seen;
    if (!ctx->enabled || !ctx->supported || (ctx->wdt_port == NULL) || (ctx->wdt_port->feed == NULL)) {
        ++ctx->stats.unsupported_rejects;
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_UNSUPPORTED;
        return EV_OK;
    }
    if (ctx->liveness_fn == NULL) {
        ++ctx->stats.health_rejects;
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_NO_LIVENESS;
        return EV_OK;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    rc = ctx->liveness_fn(ctx->liveness_ctx, &snapshot);
    if (rc != EV_OK) {
        ++ctx->stats.liveness_errors;
        ++ctx->stats.health_rejects;
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_LIVENESS_ERROR;
        return EV_OK;
    }

    if (!ev_watchdog_snapshot_is_healthy(ctx, &snapshot, &reject_reason)) {
        ++ctx->stats.health_rejects;
        ctx->stats.last_reject_reason = reject_reason;
        ctx->last_snapshot = snapshot;
        ctx->has_last_snapshot = true;
        return EV_OK;
    }

    ++ctx->stats.feed_attempts;
    rc = ctx->wdt_port->feed(ctx->wdt_port->ctx);
    if (rc != EV_OK) {
        ++ctx->stats.feeds_failed;
        ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_FEED_FAILED;
        ctx->last_snapshot = snapshot;
        ctx->has_last_snapshot = true;
        return EV_OK;
    }

    ++ctx->stats.feeds_ok;
    ctx->stats.last_reject_reason = EV_WATCHDOG_REJECT_NONE;
    ctx->last_snapshot = snapshot;
    ctx->has_last_snapshot = true;
    return EV_OK;
}

const ev_watchdog_actor_stats_t *ev_watchdog_actor_stats(const ev_watchdog_actor_ctx_t *ctx)
{
    return (ctx != NULL) ? &ctx->stats : NULL;
}
