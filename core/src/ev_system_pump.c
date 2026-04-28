#include "ev/system_pump.h"

#include <string.h>

static void ev_system_pump_record_pending_high_watermark(ev_system_pump_t *pump, size_t pending)
{
    if ((pump != NULL) && (pending > pump->stats.pending_high_watermark)) {
        pump->stats.pending_high_watermark = pending;
    }
}

static void ev_system_pump_record_call_maxima(ev_system_pump_t *pump, const ev_system_pump_report_t *report)
{
    if ((pump == NULL) || (report == NULL)) {
        return;
    }

    if (report->domains_examined > pump->stats.max_domains_examined_per_call) {
        pump->stats.max_domains_examined_per_call = report->domains_examined;
    }
    if (report->domains_pumped > pump->stats.max_domains_pumped_per_call) {
        pump->stats.max_domains_pumped_per_call = report->domains_pumped;
    }
    if (report->turns_processed > pump->stats.max_turns_per_call) {
        pump->stats.max_turns_per_call = report->turns_processed;
    }
    if (report->messages_processed > pump->stats.max_messages_per_call) {
        pump->stats.max_messages_per_call = report->messages_processed;
    }
}

static bool ev_system_pump_domain_is_valid(ev_execution_domain_t domain)
{
    return (domain >= 0) && (domain < EV_DOMAIN_COUNT);
}

static size_t ev_system_pump_cursor_advance(size_t cursor)
{
    ++cursor;
    if (cursor >= EV_DOMAIN_COUNT) {
        cursor = 0U;
    }
    return cursor;
}

void ev_system_pump_report_reset(ev_system_pump_report_t *report)
{
    if (report == NULL) {
        return;
    }

    memset(report, 0, sizeof(*report));
    report->last_domain = EV_DOMAIN_COUNT;
    report->stop_result = EV_OK;
}

ev_result_t ev_system_pump_init(ev_system_pump_t *pump)
{
    if (pump == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(pump, 0, sizeof(*pump));
    pump->stats.last_domain = EV_DOMAIN_COUNT;
    pump->stats.last_result = EV_OK;
    return EV_OK;
}

ev_result_t ev_system_pump_bind(ev_system_pump_t *pump, ev_domain_pump_t *domain_pump)
{
    ev_execution_domain_t domain;

    if ((pump == NULL) || (domain_pump == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    domain = domain_pump->domain;
    if (!ev_system_pump_domain_is_valid(domain)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (pump->slots[domain] != NULL) {
        return EV_ERR_STATE;
    }

    pump->slots[domain] = domain_pump;
    return EV_OK;
}

ev_result_t ev_system_pump_reset_stats(ev_system_pump_t *pump)
{
    if (pump == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&pump->stats, 0, sizeof(pump->stats));
    pump->stats.last_domain = EV_DOMAIN_COUNT;
    pump->stats.last_result = EV_OK;
    return EV_OK;
}

const ev_system_pump_stats_t *ev_system_pump_stats(const ev_system_pump_t *pump)
{
    return (pump != NULL) ? &pump->stats : NULL;
}

size_t ev_system_pump_bound_count(const ev_system_pump_t *pump)
{
    size_t count = 0U;
    size_t i;

    if (pump == NULL) {
        return 0U;
    }

    for (i = 0U; i < EV_DOMAIN_COUNT; ++i) {
        if (pump->slots[i] != NULL) {
            ++count;
        }
    }

    return count;
}

size_t ev_system_pump_pending(const ev_system_pump_t *pump)
{
    size_t total = 0U;
    size_t i;

    if (pump == NULL) {
        return 0U;
    }

    for (i = 0U; i < EV_DOMAIN_COUNT; ++i) {
        const ev_domain_pump_t *domain_pump = pump->slots[i];
        if (domain_pump != NULL) {
            total += ev_domain_pump_pending(domain_pump);
        }
    }

    return total;
}

ev_result_t ev_system_pump_run(
    ev_system_pump_t *pump,
    size_t turn_budget,
    ev_system_pump_report_t *report)
{
    ev_system_pump_report_t local_report;
    size_t pending_before;
    size_t remaining_turns;
    bool made_progress = false;

    if ((pump == NULL) || (turn_budget == 0U)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_system_pump_bound_count(pump) == 0U) {
        return EV_ERR_STATE;
    }

    if (report == NULL) {
        report = &local_report;
    }
    ev_system_pump_report_reset(report);
    report->turn_budget = turn_budget;

    ++pump->stats.run_calls;
    pump->stats.last_turn_budget = turn_budget;
    pump->stats.last_turns_processed = 0U;

    pending_before = ev_system_pump_pending(pump);
    report->pending_before = pending_before;
    ev_system_pump_record_pending_high_watermark(pump, pending_before);
    if (pending_before == 0U) {
        ++pump->stats.empty_calls;
        pump->stats.last_result = EV_ERR_EMPTY;
        report->pending_after = 0U;
        report->stop_result = EV_ERR_EMPTY;
        ev_system_pump_record_call_maxima(pump, report);
        return EV_ERR_EMPTY;
    }

    remaining_turns = turn_budget;
    if (pump->next_domain_index >= EV_DOMAIN_COUNT) {
        pump->next_domain_index = 0U;
    }

    while (remaining_turns > 0U) {
        bool progressed_this_pass = false;
        size_t offset;
        size_t index = pump->next_domain_index;

        for (offset = 0U; offset < EV_DOMAIN_COUNT;
             ++offset, index = ev_system_pump_cursor_advance(index)) {
            ev_domain_pump_t *domain_pump = pump->slots[index];
            ev_domain_pump_report_t domain_report = {0};
            size_t domain_budget;
            ev_result_t rc;

            if (domain_pump == NULL) {
                continue;
            }

            ++report->domains_examined;
            if (ev_domain_pump_pending(domain_pump) == 0U) {
                continue;
            }

            domain_budget = ev_domain_pump_default_budget(domain_pump);
            if (domain_budget == 0U) {
                pump->stats.last_domain = domain_pump->domain;
                pump->stats.last_result = EV_ERR_CONTRACT;
                report->last_domain = domain_pump->domain;
                report->pending_after = ev_system_pump_pending(pump);
                report->stop_result = EV_ERR_CONTRACT;
                ev_system_pump_record_call_maxima(pump, report);
                return EV_ERR_CONTRACT;
            }

            rc = ev_domain_pump_run(domain_pump, domain_budget, &domain_report);
            pump->next_domain_index = ev_system_pump_cursor_advance(index);
            pump->stats.last_domain = domain_pump->domain;
            report->last_domain = domain_pump->domain;

            if (rc == EV_ERR_EMPTY) {
                continue;
            }

            if (rc != EV_OK) {
                report->domains_pumped += 1U;
                report->messages_processed += domain_report.processed;
                report->turns_processed += 1U;
                report->pending_after = ev_system_pump_pending(pump);
                pump->stats.domains_pumped += 1U;
                pump->stats.turns_processed += 1U;
                pump->stats.messages_processed += domain_report.processed;
                pump->stats.last_turns_processed = report->turns_processed;
                pump->stats.last_result = rc;
                report->stop_result = rc;
                ev_system_pump_record_call_maxima(pump, report);
                return rc;
            }

            ++report->domains_pumped;
            report->messages_processed += domain_report.processed;
            ++report->turns_processed;
            ++pump->stats.domains_pumped;
            pump->stats.messages_processed += domain_report.processed;
            progressed_this_pass = true;
            made_progress = true;
            --remaining_turns;

            if (remaining_turns == 0U) {
                break;
            }
        }

        if (!progressed_this_pass) {
            break;
        }
    }

    report->pending_after = ev_system_pump_pending(pump);
    pump->stats.turns_processed += report->turns_processed;
    pump->stats.last_turns_processed = report->turns_processed;

    if (!made_progress) {
        pump->stats.last_result = EV_ERR_STATE;
        report->stop_result = EV_ERR_STATE;
        ev_system_pump_record_call_maxima(pump, report);
        return EV_ERR_STATE;
    }

    if ((remaining_turns == 0U) && (report->pending_after > 0U)) {
        report->exhausted_turn_budget = true;
        ++pump->stats.budget_hits;
        pump->stats.last_result = EV_OK;
        report->stop_result = EV_OK;
        ev_system_pump_record_call_maxima(pump, report);
        return EV_OK;
    }

    pump->stats.last_result = EV_OK;
    report->stop_result = EV_ERR_EMPTY;
    ev_system_pump_record_call_maxima(pump, report);
    return EV_OK;
}
