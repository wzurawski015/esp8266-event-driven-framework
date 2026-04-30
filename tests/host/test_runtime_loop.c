#include <assert.h>
#include <string.h>

#include "ev/runtime_loop.h"

static ev_result_t fake_now(void *ctx, uint32_t *out_now_ms)
{
    uint32_t *now = (uint32_t *)ctx;
    if ((now == 0) || (out_now_ms == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    *out_now_ms = *now;
    return EV_OK;
}

static ev_result_t fake_collect(ev_runtime_graph_t *graph,
                                void *context,
                                ev_runtime_loop_report_t *report,
                                const ev_runtime_loop_policy_t *policy)
{
    (void)graph;
    (void)policy;
    if ((context == 0) || (report == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    if (*(uint32_t *)context == 0U) {
        (*(uint32_t *)context)++;
        report->irq_samples++;
    }
    return EV_OK;
}

static ev_result_t timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_runtime_graph_t *graph = (ev_runtime_graph_t *)ctx;
    return ev_runtime_graph_send(graph, target_actor, msg);
}

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_runtime_loop_policy_t policy;
    ev_runtime_loop_ports_t ports;
    ev_runtime_loop_report_t report;
    ev_timer_token_t token;
    uint32_t now = 10U;
    uint32_t collect_state = 0U;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    ev_runtime_loop_policy_default(&policy);
    memset(&ports, 0, sizeof(ports));
    ports.now_ms = fake_now;
    ports.now_ctx = &now;
    ports.collect_ingress = fake_collect;
    ports.collect_ctx = &collect_state;
    ports.timer_delivery = timer_delivery;
    ports.timer_delivery_ctx = &graph;

    assert(ev_runtime_graph_schedule_periodic(&graph, now, 10U, ACT_METRICS, EV_TICK_1S, 0U, &token) == EV_OK);
    assert(ev_runtime_loop_poll_once(&graph, &policy, &ports, &report) == EV_OK);
    assert(report.irq_samples == 1U);
    assert(report.timers_published == 1U);
    assert(report.messages >= 1U);
    assert(report.partial == 0U);
    return 0;
}
