#include <assert.h>

#include "ev/runtime_graph.h"
#include "ev/runtime_poll.h"
#include "ev/power_manager.h"

static ev_result_t timer_sink(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    size_t *count = (size_t *)ctx;
    assert(target_actor == ACT_FAULT);
    assert(msg->event_id == EV_TICK_1S);
    (*count)++;
    return EV_OK;
}

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_timer_token_t token;
    size_t published = 0U;
    ev_quiescence_report_t report;
    ev_power_policy_t policy = {1U, 0U, 1U, 10U, 10000U, 0U, 0U, 0U, 0U, 0U};
    ev_power_manager_t power;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    assert(ev_timer_schedule_oneshot(&graph.timer_service, 100U, 50U, ACT_FAULT, EV_TICK_1S, 0U, &token) == EV_OK);
    assert(ev_timer_pending_count(&graph.timer_service) == 1U);
    assert(ev_timer_publish_due(&graph.timer_service, 149U, timer_sink, &published, 4U) == 0U);
    assert(ev_timer_publish_due(&graph.timer_service, 150U, timer_sink, &published, 4U) == 1U);
    assert(published == 1U);
    assert(ev_timer_pending_count(&graph.timer_service) == 0U);

    ev_power_manager_init(&power, &policy);
    assert(ev_runtime_is_quiescent(&graph, &report) == EV_OK);
    assert(ev_power_manager_can_sleep(&graph, &power, 100U, &report) == EV_OK);
    return 0;
}
