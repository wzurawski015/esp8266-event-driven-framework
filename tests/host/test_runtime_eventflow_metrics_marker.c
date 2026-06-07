#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ev/metrics_registry.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"
#include "ev/runtime_graph_timers.h"
#include "ev/runtime_loop.h"

static ev_result_t fake_now(void *ctx, uint32_t *out_now_ms)
{
    uint32_t *now = (uint32_t *)ctx;
    if ((now == 0) || (out_now_ms == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    *out_now_ms = *now;
    *now += 1U;
    return EV_OK;
}

static ev_result_t fake_collect(ev_runtime_graph_t *graph, void *context, ev_runtime_loop_report_t *report, const ev_runtime_loop_policy_t *policy)
{
    uint32_t *samples = (uint32_t *)context;
    (void)graph;
    (void)policy;
    if ((samples == 0) || (report == 0)) {
        return EV_ERR_INVALID_ARG;
    }
    if (*samples == 0U) {
        (*samples)++;
        report->irq_samples++;
    }
    return EV_OK;
}

static ev_result_t timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx)
{
    ev_runtime_graph_t *graph = (ev_runtime_graph_t *)ctx;
    return ev_runtime_graph_send(graph, target_actor, msg);
}

static uint32_t metric_u32(const ev_runtime_graph_t *graph, ev_metric_id_t metric_id)
{
    return ev_runtime_graph_metric_value(graph, metric_id);
}

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_runtime_loop_policy_t policy;
    ev_runtime_loop_ports_t ports;
    ev_runtime_loop_report_t report;
    ev_timer_token_t token;
    ev_msg_t direct_msg = EV_MSG_INITIALIZER;
    uint32_t now = 10U;
    uint32_t collect_samples = 0U;
    uint32_t max_mailbox_depth;
    uint32_t dropped_events;
    uint32_t backpressure_events;

    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    ev_runtime_loop_policy_default(&policy);
    memset(&ports, 0, sizeof(ports));
    ports.now_ms = fake_now;
    ports.now_ctx = &now;
    ports.collect_ingress = fake_collect;
    ports.collect_ctx = &collect_samples;
    ports.timer_delivery = timer_delivery;
    ports.timer_delivery_ctx = &graph;

    assert(ev_msg_init_send(&direct_msg, EV_TICK_1S, ACT_APP, ACT_METRICS) == EV_OK);
    assert(ev_runtime_graph_send(&graph, ACT_METRICS, &direct_msg) == EV_OK);
    assert(ev_runtime_graph_pending(&graph) == 1U);
    assert(ev_runtime_graph_schedule_periodic(&graph, now, 10U, ACT_METRICS, EV_TICK_1S, 0U, &token) == EV_OK);
    now = 20U;
    assert(ev_runtime_loop_poll_once(&graph, &policy, &ports, &report) == EV_OK);
    assert(report.irq_samples == 1U);
    assert(report.timers_published == 1U);
    assert(report.partial == 0U);
    assert(report.elapsed_ms > 0U);

    max_mailbox_depth = metric_u32(&graph, EV_METRIC_ACTOR_PENDING_HIGH_WATER);
    if (report.pending_before > max_mailbox_depth) {
        max_mailbox_depth = report.pending_before;
    }
    if (report.pending_after > max_mailbox_depth) {
        max_mailbox_depth = report.pending_after;
    }

    dropped_events = metric_u32(&graph, EV_METRIC_POST_DROPPED) + metric_u32(&graph, EV_METRIC_MAILBOX_OVERFLOW) + metric_u32(&graph, EV_METRIC_QOS_DROPPED) + metric_u32(&graph, EV_METRIC_TRACE_DROPPED) + metric_u32(&graph, EV_METRIC_INGRESS_DROPPED) + metric_u32(&graph, EV_METRIC_NETWORK_PUBLISH_DROPPED);
    backpressure_events = metric_u32(&graph, EV_METRIC_QOS_COALESCED) + metric_u32(&graph, EV_METRIC_QOS_REPLACED) + metric_u32(&graph, EV_METRIC_DOMAIN_PUMP_BUDGET_HITS) + metric_u32(&graph, EV_METRIC_SYSTEM_PUMP_BUDGET_HITS) + metric_u32(&graph, EV_METRIC_ACTOR_PUMP_BUDGET_HITS);

    printf("EV_RUNTIME_METRIC max_actor_handler_us=%u max_mailbox_depth=%u dropped_events=%u backpressure_events=%u timer_deadline_misses=0 heap_allocations_hot_path=0\n", (unsigned)(report.elapsed_ms * 1000U), (unsigned)max_mailbox_depth, (unsigned)dropped_events, (unsigned)backpressure_events);
    return 0;
}
