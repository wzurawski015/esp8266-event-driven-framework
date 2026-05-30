#include "ev/active_route_table.h"

#include <string.h>

#include "ev/event_catalog.h"

static ev_route_span_t ev_active_route_empty_span(void)
{
    ev_route_span_t span = {0U, 0U};
    return span;
}

static int ev_active_route_span_is_valid(const ev_active_route_table_t *table, ev_route_span_t span)
{
    if (table == NULL) {
        return 0;
    }
    return (span.start_index <= table->count) &&
           (span.count <= (table->count - span.start_index));
}

void ev_active_route_table_init(ev_active_route_table_t *table)
{
    if (table != NULL) {
        (void)memset(table, 0, sizeof(*table));
    }
}

ev_result_t ev_active_route_table_add(ev_active_route_table_t *table, const ev_route_t *route, ev_active_route_state_t state, ev_result_t reason)
{
    if ((table == NULL) || (route == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (table->count >= EV_ACTIVE_ROUTE_TABLE_CAPACITY) {
        return EV_ERR_FULL;
    }
    table->entries[table->count].route = *route;
    table->entries[table->count].state = state;
    table->entries[table->count].reason = reason;
    table->count++;
    table->spans_finalized = 0U;
    if (state == EV_ACTIVE_ROUTE_ENABLED) {
        table->active_count++;
    } else if (state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
        table->optional_disabled_count++;
    } else {
        table->rejected_count++;
    }
    return EV_OK;
}

ev_result_t ev_active_route_table_finalize_spans(ev_active_route_table_t *table)
{
    uint8_t closed[EV_EVENT_COUNT];
    ev_event_id_t previous_event = EV_EVENT_COUNT;
    int have_previous_event = 0;
    size_t i;

    if (table == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(table->spans, 0, sizeof(table->spans));
    (void)memset(closed, 0, sizeof(closed));
    table->spans_finalized = 0U;

    for (i = 0U; i < table->count; ++i) {
        const ev_event_id_t event_id = table->entries[i].route.event_id;
        ev_route_span_t *span;

        if (!ev_event_id_is_valid(event_id)) {
            return EV_ERR_OUT_OF_RANGE;
        }

        if ((have_previous_event != 0) && (event_id != previous_event)) {
            closed[previous_event] = 1U;
        }

        span = &table->spans[event_id];
        if (span->count == 0U) {
            if (closed[event_id] != 0U) {
                return EV_ERR_CONTRACT;
            }
            span->start_index = i;
            span->count = 1U;
        } else {
            if ((closed[event_id] != 0U) || ((span->start_index + span->count) != i)) {
                return EV_ERR_CONTRACT;
            }
            span->count++;
        }

        previous_event = event_id;
        have_previous_event = 1;
    }

    table->spans_finalized = 1U;
    return EV_OK;
}

ev_route_span_t ev_active_route_table_span_for_event(const ev_active_route_table_t *table, ev_event_id_t event_id)
{
    ev_route_span_t span;

    if ((table == NULL) || (table->spans_finalized == 0U) || !ev_event_id_is_valid(event_id)) {
        return ev_active_route_empty_span();
    }

    span = table->spans[event_id];
    if (ev_active_route_span_is_valid(table, span) == 0) {
        return ev_active_route_empty_span();
    }

    return span;
}

const ev_active_route_t *ev_active_route_at(const ev_active_route_table_t *table, size_t index)
{
    if ((table == NULL) || (index >= table->count)) {
        return NULL;
    }
    return &table->entries[index];
}

const char *ev_active_route_state_name(ev_active_route_state_t state)
{
    switch (state) {
    case EV_ACTIVE_ROUTE_ENABLED:
        return "enabled";
    case EV_ACTIVE_ROUTE_OPTIONAL_DISABLED:
        return "optional_disabled";
    case EV_ACTIVE_ROUTE_REJECTED_INVALID_EVENT:
        return "rejected_invalid_event";
    case EV_ACTIVE_ROUTE_REJECTED_INVALID_ACTOR:
        return "rejected_invalid_actor";
    case EV_ACTIVE_ROUTE_REJECTED_MISSING_MANDATORY_ACTOR:
        return "rejected_missing_mandatory_actor";
    case EV_ACTIVE_ROUTE_REJECTED_QOS_CONFLICT:
        return "rejected_qos_conflict";
    case EV_ACTIVE_ROUTE_REJECTED_OVERFLOW:
        return "rejected_overflow";
    default:
        return "unknown";
    }
}

int ev_route_qos_is_valid(ev_route_qos_t qos)
{
    return (qos == EV_ROUTE_QOS_CRITICAL) ||
           (qos == EV_ROUTE_QOS_BEST_EFFORT) ||
           (qos == EV_ROUTE_QOS_LOSSY) ||
           (qos == EV_ROUTE_QOS_COALESCED) ||
           (qos == EV_ROUTE_QOS_LATEST_ONLY) ||
           (qos == EV_ROUTE_QOS_WAKEUP_CRITICAL) ||
           (qos == EV_ROUTE_QOS_TELEMETRY) ||
           (qos == EV_ROUTE_QOS_COMMAND);
}
