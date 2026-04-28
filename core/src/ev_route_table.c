#include "ev/route_table.h"

#include "ev/actor_catalog.h"
#include "ev/compiler.h"
#include "ev/event_catalog.h"
#include "ev/route_table_generated.h"

EV_STATIC_ASSERT(EV_ROUTE_TABLE_GENERATED_COUNT > 0U, "route table must not be empty");
EV_STATIC_ASSERT(EV_ARRAY_LEN(k_ev_route_spans_generated) == EV_EVENT_COUNT, "route span table must cover all events");
EV_STATIC_ASSERT(EV_ARRAY_LEN(k_ev_route_table_generated) == EV_ROUTE_TABLE_GENERATED_COUNT, "route table count must match generator metadata");

static bool ev_route_span_is_valid(ev_route_span_t span)
{
    return (span.start_index <= EV_ARRAY_LEN(k_ev_route_table_generated)) &&
           (span.count <= (EV_ARRAY_LEN(k_ev_route_table_generated) - span.start_index));
}

size_t ev_route_count(void)
{
    return EV_ARRAY_LEN(k_ev_route_table_generated);
}

const ev_route_t *ev_route_at(size_t index)
{
    if (index >= EV_ARRAY_LEN(k_ev_route_table_generated)) {
        return NULL;
    }

    return &k_ev_route_table_generated[index];
}

ev_route_span_t ev_route_span_for_event(ev_event_id_t event_id)
{
    ev_route_span_t empty_span = {0U, 0U};

    if (!ev_event_id_is_valid(event_id)) {
        return empty_span;
    }

    if (!ev_route_span_is_valid(k_ev_route_spans_generated[event_id])) {
        return empty_span;
    }

    return k_ev_route_spans_generated[event_id];
}

size_t ev_route_count_for_event(ev_event_id_t event_id)
{
    return ev_route_span_for_event(event_id).count;
}

bool ev_route_exists(ev_event_id_t event_id, ev_actor_id_t target_actor)
{
    ev_route_span_t span;
    size_t i;

    if (!ev_actor_id_is_valid(target_actor)) {
        return false;
    }

    span = ev_route_span_for_event(event_id);
    for (i = 0U; i < span.count; ++i) {
        const ev_route_t *route = &k_ev_route_table_generated[span.start_index + i];
        if (route->target_actor == target_actor) {
            return true;
        }
    }

    return false;
}


const char *ev_route_qos_name(ev_route_qos_t qos)
{
    switch (qos) {
    case EV_ROUTE_QOS_CRITICAL:
        return "critical";
    case EV_ROUTE_QOS_BEST_EFFORT:
        return "best_effort";
    case EV_ROUTE_QOS_LOSSY:
        return "lossy";
    case EV_ROUTE_QOS_COALESCED:
        return "coalesced";
    case EV_ROUTE_QOS_LATEST_ONLY:
        return "latest_only";
    case EV_ROUTE_QOS_WAKEUP_CRITICAL:
        return "wakeup_critical";
    case EV_ROUTE_QOS_TELEMETRY:
        return "telemetry";
    case EV_ROUTE_QOS_COMMAND:
        return "command";
    default:
        return "invalid";
    }
}
