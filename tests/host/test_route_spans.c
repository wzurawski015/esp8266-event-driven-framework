#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "ev/actor_catalog.h"
#include "ev/event_catalog.h"
#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/route_table.h"

#define TEST_MAX_DELIVERIES 8U

typedef struct {
    size_t call_count;
    ev_actor_id_t targets[TEST_MAX_DELIVERIES];
} publish_trace_t;

static ev_result_t trace_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    publish_trace_t *trace = (publish_trace_t *)context;
    (void)msg;

    if (trace->call_count < TEST_MAX_DELIVERIES) {
        trace->targets[trace->call_count] = target_actor;
    }
    ++trace->call_count;

    return EV_OK;
}

static void assert_span_is_in_bounds(ev_route_span_t span)
{
    assert(span.start_index <= ev_route_count());
    assert(span.count <= (ev_route_count() - span.start_index));
}

static void assert_span_routes_match_event(ev_event_id_t event_id)
{
    ev_route_span_t span = ev_route_span_for_event(event_id);
    size_t i;

    assert_span_is_in_bounds(span);
    for (i = 0U; i < span.count; ++i) {
        const ev_route_t *route = ev_route_at(span.start_index + i);
        assert(route != NULL);
        assert(route->event_id == event_id);
        assert(ev_actor_id_is_valid(route->target_actor));
    }
}

static void assert_spans_are_non_overlapping(void)
{
    ev_event_id_t event_id;
    size_t route_index;

    for (route_index = 0U; route_index < ev_route_count(); ++route_index) {
        size_t covering_spans = 0U;

        for (event_id = (ev_event_id_t)0; event_id < EV_EVENT_COUNT; event_id = (ev_event_id_t)(event_id + 1)) {
            ev_route_span_t span = ev_route_span_for_event(event_id);
            assert_span_is_in_bounds(span);
            if ((span.count != 0U) &&
                (route_index >= span.start_index) &&
                (route_index < (span.start_index + span.count))) {
                ++covering_spans;
            }
        }

        assert(covering_spans == 1U);
    }
}

static void assert_publish_targets(ev_event_id_t event_id, ev_actor_id_t expected_actor)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_publish_report_t report;
    publish_trace_t trace = {0};

    ev_publish_report_reset(&report);
    assert(ev_msg_init_publish(&msg, event_id, ACT_APP) == EV_OK);
    assert(ev_publish_ex(&msg, trace_delivery, &trace, EV_PUBLISH_FAIL_FAST, &report) == EV_OK);
    assert(report.matched_routes == 1U);
    assert(report.attempted_deliveries == 1U);
    assert(report.delivered_count == 1U);
    assert(report.failed_count == 0U);
    assert(trace.call_count == 1U);
    assert(trace.targets[0] == expected_actor);
}

int main(void)
{
    ev_event_id_t event_id;
    ev_route_span_t span;

    assert(ev_route_count() == 53U);

    for (event_id = (ev_event_id_t)0; event_id < EV_EVENT_COUNT; event_id = (ev_event_id_t)(event_id + 1)) {
        assert_span_routes_match_event(event_id);
    }
    assert_spans_are_non_overlapping();

    span = ev_route_span_for_event(EV_SYSTEM_READY);
    assert(span.count == 1U);
    assert(ev_route_exists(EV_SYSTEM_READY, ACT_APP));

    span = ev_route_span_for_event(EV_SYS_GOTO_SLEEP_CMD);
    assert(span.count == 1U);
    assert(ev_route_exists(EV_SYS_GOTO_SLEEP_CMD, ACT_POWER));
    assert(ev_route_exists(EV_NET_WIFI_UP, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_TX_CMD, ACT_NETWORK));
    assert(ev_route_exists(EV_FAULT_REPORTED, ACT_FAULT));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX_LEASE, ACT_NETWORK));
    assert(ev_route_exists(EV_NET_MQTT_MSG_RX_LEASE, ACT_COMMAND));

    span = ev_route_span_for_event((ev_event_id_t)EV_EVENT_COUNT);
    assert(span.start_index == 0U);
    assert(span.count == 0U);

    span = ev_route_span_for_event(EV_TICK_1S);
    assert(span.count == 9U);
    assert(ev_route_exists(EV_TICK_1S, ACT_WATCHDOG));
    assert(ev_route_exists(EV_TICK_1S, ACT_NETWORK));
    assert(ev_route_exists(EV_TICK_1S, ACT_COMMAND));

    assert_publish_targets(EV_SYSTEM_READY, ACT_APP);
    assert_publish_targets(EV_SYS_GOTO_SLEEP_CMD, ACT_POWER);

    return 0;
}
