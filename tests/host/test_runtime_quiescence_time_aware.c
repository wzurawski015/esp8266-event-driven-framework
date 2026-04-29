#include <assert.h>

#include "ev/runtime_graph.h"
#include "ev/power_manager.h"

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_timer_token_t token;
    ev_quiescence_report_t report;
    ev_quiescence_policy_t policy = {EV_QUIESCENCE_BUFFER_BLOCK_UNTIL_DRAINED, EV_QUIESCENCE_BUFFER_BLOCK_CRITICAL_ONLY, EV_QUIESCENCE_BUFFER_BLOCK_NEVER, 0U, 0U, 1U, 1U};
    ev_trace_record_t trace = {0U, EV_TICK_1S, ACT_APP, ACT_FAULT, EV_OK, EV_ROUTE_QOS_CRITICAL, 0U, 0U};

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS | EV_CAP_TRACE, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS | EV_CAP_TRACE) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    assert(ev_timer_schedule_oneshot(&graph.timer_service, 100U, 50U, ACT_FAULT, EV_TICK_1S, 0U, &token) == EV_OK);
    assert(ev_runtime_is_quiescent_at(&graph, 149U, &policy, &report) == EV_OK);
    assert(report.due_timers == 0U);
    assert(report.next_deadline_ms == 150U);
    assert(ev_runtime_is_quiescent_at(&graph, 150U, &policy, &report) == EV_ERR_NOT_READY);
    assert(report.due_timers == 1U);
    assert((report.policy_blocker_mask & EV_QUIESCENCE_POLICY_BLOCK_DUE_TIMER) != 0U);

    assert(ev_trace_record(&graph.trace_ring, &trace) == EV_OK);
    assert(ev_runtime_is_quiescent_at(&graph, 10U, &policy, &report) == EV_ERR_NOT_READY);
    assert((report.policy_blocker_mask & EV_QUIESCENCE_POLICY_BLOCK_TRACE) != 0U);
    ev_trace_clear(&graph.trace_ring);
    return 0;
}
