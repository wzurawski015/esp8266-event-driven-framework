#include "ev/active_route_table.h"

#include <string.h>

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
    if (state == EV_ACTIVE_ROUTE_ENABLED) {
        table->active_count++;
    } else if (state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
        table->optional_disabled_count++;
    } else {
        table->rejected_count++;
    }
    return EV_OK;
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
