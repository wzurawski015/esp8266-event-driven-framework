#include "ev/domain_pump.h"

#include <string.h>

#include "ev/actor_catalog.h"

static bool ev_domain_pump_domain_is_valid(ev_execution_domain_t domain)
{
    switch (domain) {
    case EV_DOMAIN_ISR:
    case EV_DOMAIN_FAST_LOOP:
    case EV_DOMAIN_SLOW_IO:
    case EV_DOMAIN_NETWORK:
        return true;
    default:
        return false;
    }
}

static bool ev_domain_pump_actor_matches(ev_actor_id_t actor_id, ev_execution_domain_t domain)
{
    const ev_actor_meta_t *meta = ev_actor_meta(actor_id);
    return (meta != NULL) && (meta->execution_domain == domain);
}

static size_t ev_domain_pump_cursor_advance(size_t cursor, size_t count)
{
    if (count == 0U) {
        return 0U;
    }

    ++cursor;
    if (cursor >= count) {
        cursor = 0U;
    }
    return cursor;
}

static void ev_domain_pump_record_pending_high_watermark(ev_domain_pump_t *pump, size_t pending)
{
    if ((pump != NULL) && (pending > pump->stats.pending_high_watermark)) {
        pump->stats.pending_high_watermark = pending;
    }
}

static void ev_domain_pump_record_call_maxima(ev_domain_pump_t *pump, const ev_domain_pump_report_t *report)
{
    if ((pump == NULL) || (report == NULL)) {
        return;
    }

    if (report->actors_examined > pump->stats.max_actors_examined_per_call) {
        pump->stats.max_actors_examined_per_call = report->actors_examined;
    }
    if (report->actors_pumped > pump->stats.max_actors_pumped_per_call) {
        pump->stats.max_actors_pumped_per_call = report->actors_pumped;
    }
    if (report->processed > pump->stats.max_messages_per_call) {
        pump->stats.max_messages_per_call = report->processed;
    }
}

static size_t ev_domain_pump_domain_default_budget(ev_execution_domain_t domain)
{
    size_t total = 0U;
    size_t i;

    if (!ev_domain_pump_domain_is_valid(domain)) {
        return 0U;
    }

    for (i = 0U; i < ev_actor_count(); ++i) {
        const ev_actor_meta_t *meta = ev_actor_meta((ev_actor_id_t)i);
        if ((meta != NULL) && (meta->execution_domain == domain)) {
            total += meta->drain_budget;
        }
    }

    return total;
}


void ev_domain_pump_report_reset(ev_domain_pump_report_t *report)
{
    if (report == NULL) {
        return;
    }

    memset(report, 0, sizeof(*report));
    report->last_actor = EV_ACTOR_NONE;
    report->stop_result = EV_OK;
}

ev_result_t ev_domain_pump_init(
    ev_domain_pump_t *pump,
    ev_actor_registry_t *registry,
    ev_execution_domain_t domain)
{
    if ((pump == NULL) || (registry == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_domain_pump_domain_is_valid(domain)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    memset(pump, 0, sizeof(*pump));
    pump->registry = registry;
    pump->domain = domain;
    pump->stats.last_actor = EV_ACTOR_NONE;
    pump->stats.last_result = EV_OK;
    return EV_OK;
}

ev_result_t ev_domain_pump_reset_stats(ev_domain_pump_t *pump)
{
    if (pump == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&pump->stats, 0, sizeof(pump->stats));
    pump->stats.last_actor = EV_ACTOR_NONE;
    pump->stats.last_result = EV_OK;
    return EV_OK;
}

const ev_domain_pump_stats_t *ev_domain_pump_stats(const ev_domain_pump_t *pump)
{
    return (pump != NULL) ? &pump->stats : NULL;
}

size_t ev_domain_pump_default_budget(const ev_domain_pump_t *pump)
{
    if (pump == NULL) {
        return 0U;
    }

    return ev_domain_pump_domain_default_budget(pump->domain);
}

size_t ev_domain_pump_pending(const ev_domain_pump_t *pump)
{
    size_t total = 0U;
    size_t i;

    if ((pump == NULL) || (pump->registry == NULL)) {
        return 0U;
    }

    for (i = 0U; i < ev_actor_count(); ++i) {
        ev_actor_runtime_t *runtime;
        if (!ev_domain_pump_actor_matches((ev_actor_id_t)i, pump->domain)) {
            continue;
        }
        runtime = pump->registry->slots[i];
        if (runtime != NULL) {
            total += ev_actor_runtime_pending(runtime);
        }
    }

    return total;
}

ev_result_t ev_domain_pump_run(
    ev_domain_pump_t *pump,
    size_t budget,
    ev_domain_pump_report_t *report)
{
    ev_domain_pump_report_t local_report;
    size_t remaining;
    size_t actor_count;
    size_t pending_before;
    bool made_progress = false;

    if ((pump == NULL) || (pump->registry == NULL) || (budget == 0U)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_domain_pump_domain_is_valid(pump->domain)) {
        return EV_ERR_CONTRACT;
    }

    if (report == NULL) {
        report = &local_report;
    }
    ev_domain_pump_report_reset(report);
    report->budget = budget;

    ++pump->stats.pump_calls;
    pump->stats.last_budget = budget;
    pump->stats.last_processed = 0U;

    pending_before = ev_domain_pump_pending(pump);
    report->pending_before = pending_before;
    ev_domain_pump_record_pending_high_watermark(pump, pending_before);
    if (pending_before == 0U) {
        ++pump->stats.pump_empty_calls;
        pump->stats.last_result = EV_ERR_EMPTY;
        report->pending_after = 0U;
        report->stop_result = EV_ERR_EMPTY;
        ev_domain_pump_record_call_maxima(pump, report);
        return EV_ERR_EMPTY;
    }

    remaining = budget;
    actor_count = ev_actor_count();
    if (pump->next_actor_index >= actor_count) {
        pump->next_actor_index = 0U;
    }

    while (remaining > 0U) {
        bool progressed_this_pass = false;
        size_t offset;
        size_t start_index = pump->next_actor_index;
        size_t index = start_index;

        for (offset = 0U; offset < actor_count;
             ++offset, index = ev_domain_pump_cursor_advance(index, actor_count)) {
            ev_actor_runtime_t *runtime;
            size_t actor_budget;
            ev_actor_pump_report_t actor_report = {0};
            ev_result_t rc;

            if (!ev_domain_pump_actor_matches((ev_actor_id_t)index, pump->domain)) {
                continue;
            }

            ++report->actors_examined;
            runtime = pump->registry->slots[index];
            if (runtime == NULL) {
                continue;
            }
            if (ev_actor_runtime_pending(runtime) == 0U) {
                continue;
            }

            actor_budget = ev_actor_runtime_default_budget(runtime);
            if (actor_budget == 0U) {
                pump->stats.last_actor = runtime->actor_id;
                pump->stats.last_result = EV_ERR_CONTRACT;
                report->last_actor = runtime->actor_id;
                report->pending_after = ev_domain_pump_pending(pump);
                report->stop_result = EV_ERR_CONTRACT;
                ev_domain_pump_record_call_maxima(pump, report);
                return EV_ERR_CONTRACT;
            }
            if (actor_budget > remaining) {
                actor_budget = remaining;
            }

            rc = ev_actor_runtime_pump(runtime, actor_budget, &actor_report);
            pump->next_actor_index = ev_domain_pump_cursor_advance(index, actor_count);
            pump->stats.last_actor = runtime->actor_id;
            report->last_actor = runtime->actor_id;

            if (rc == EV_ERR_EMPTY) {
                continue;
            }

            if (rc != EV_OK) {
                report->processed += actor_report.processed;
                report->actors_pumped += 1U;
                report->pending_after = ev_domain_pump_pending(pump);
                report->stop_result = rc;
                pump->stats.last_processed = report->processed;
                pump->stats.last_result = rc;
                ev_domain_pump_record_call_maxima(pump, report);
                return rc;
            }

            report->processed += actor_report.processed;
            report->actors_pumped += 1U;
            remaining -= actor_report.processed;
            progressed_this_pass = true;
            made_progress = true;

            if (remaining == 0U) {
                break;
            }
        }

        if (!progressed_this_pass) {
            break;
        }
    }

    report->pending_after = ev_domain_pump_pending(pump);
    pump->stats.last_processed = report->processed;

    if (!made_progress) {
        pump->stats.last_result = EV_ERR_STATE;
        report->stop_result = EV_ERR_STATE;
        ev_domain_pump_record_call_maxima(pump, report);
        return EV_ERR_STATE;
    }

    if ((remaining == 0U) && (report->pending_after > 0U)) {
        report->exhausted_budget = true;
        ++pump->stats.pump_budget_hits;
        pump->stats.last_result = EV_OK;
        report->stop_result = EV_OK;
        ev_domain_pump_record_call_maxima(pump, report);
        return EV_OK;
    }

    pump->stats.last_result = EV_OK;
    report->stop_result = EV_ERR_EMPTY;
    ev_domain_pump_record_call_maxima(pump, report);
    return EV_OK;
}
