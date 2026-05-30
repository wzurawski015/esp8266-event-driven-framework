#include <assert.h>
#include <string.h>

#include "ev/actor_mailbox_layout_generated.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"
#include "ev/runtime_graph_timers.h"
#include "ev/runtime_graph_trace.h"
#include "ev/runtime_poll.h"

static ev_runtime_graph_t g_static_graph;

static ev_result_t opaque_timer_sink(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    size_t *count = (size_t *)ctx;
    assert(target_actor == ACT_METRICS);
    assert(msg != NULL);
    assert(msg->event_id == EV_TICK_1S);
    if (count != NULL) {
        (*count)++;
    }
    return EV_OK;
}

static void build_small_graph(ev_runtime_graph_t *graph)
{
    ev_runtime_builder_t builder;

    assert(ev_runtime_builder_init(&builder,
                                   graph,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS | EV_CAP_TRACE,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS | EV_CAP_TRACE) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
}

static void assert_public_api_operates_on_opaque_graph(ev_runtime_graph_t *graph)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t delivery;
    ev_runtime_poll_report_t poll;
    ev_timer_token_t token;
    ev_trace_record_t trace_in;
    ev_trace_record_t trace_out;
    size_t fault_offset = 0U;
    size_t metrics_offset = 0U;
    size_t timer_count = 0U;

    build_small_graph(graph);

    assert(ev_runtime_graph_actor_mailbox_capacity(graph, ACT_FAULT) == 8U);
    assert(ev_runtime_graph_actor_mailbox_capacity(graph, ACT_METRICS) == 8U);
    assert(ev_runtime_graph_actor_mailbox_offset(graph, ACT_FAULT, &fault_offset) == EV_OK);
    assert(ev_runtime_graph_actor_mailbox_offset(graph, ACT_METRICS, &metrics_offset) == EV_OK);
    assert(fault_offset < EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(metrics_offset < EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(fault_offset != metrics_offset);

    assert(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(graph, &msg, &delivery) == EV_OK);
    assert(delivery.matched_routes == 1U);
    assert(delivery.delivered == 1U);

    assert(ev_msg_init_send(&msg, EV_COMMAND_ACCEPTED, ACT_APP, ACT_METRICS) == EV_OK);
    assert(ev_runtime_graph_send(graph, ACT_METRICS, &msg) == EV_OK);
    assert(ev_runtime_poll_once(graph, 0U, 2U, &poll) == EV_OK);
    assert(poll.messages_processed >= 1U);
    assert(ev_runtime_graph_scheduler_poll_count(graph) >= 1U);

    ev_runtime_graph_trace_clear(graph);
    memset(&trace_in, 0, sizeof(trace_in));
    trace_in.event_id = EV_COMMAND_ACCEPTED;
    trace_in.source_actor = ACT_APP;
    trace_in.target_actor = ACT_METRICS;
    trace_in.result = EV_OK;
    assert(ev_runtime_graph_trace_record(graph, &trace_in) == EV_OK);
    memset(&trace_out, 0, sizeof(trace_out));
    assert(ev_runtime_graph_trace_drain(graph, &trace_out, 1U) == 1U);
    assert(trace_out.event_id == EV_COMMAND_ACCEPTED);
    assert(trace_out.target_actor == ACT_METRICS);

    assert(ev_runtime_graph_schedule_oneshot(graph, 100U, 10U, ACT_METRICS, EV_TICK_1S, 0U, &token) == EV_OK);
    assert(ev_runtime_graph_timer_pending_count(graph) == 1U);
    assert(ev_runtime_graph_publish_due_timers(graph, 109U, opaque_timer_sink, &timer_count, 4U) == 0U);
    assert(ev_runtime_graph_publish_due_timers(graph, 110U, opaque_timer_sink, &timer_count, 4U) == 1U);
    assert(timer_count == 1U);
    assert(ev_runtime_graph_timer_pending_count(graph) == 0U);
}

int main(void)
{
    ev_runtime_graph_t stack_graph;

    assert(sizeof(ev_runtime_graph_t) >= EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES);
    assert(sizeof(ev_runtime_graph_t) <= (EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES + sizeof(long double)));
    assert(ev_runtime_graph_configured_mailbox_slots() == EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(ev_runtime_graph_configured_mailbox_bytes() == (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY * sizeof(ev_msg_t)));

    assert_public_api_operates_on_opaque_graph(&stack_graph);
    assert_public_api_operates_on_opaque_graph(&g_static_graph);
    return 0;
}
