#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bench_support.h"

#include "ev/msg.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_loop.h"
#include "ev/runtime_poll.h"

#define BENCH_POLL_ITERATIONS 20000ULL
#define BENCH_POLL_BATCHES 5000ULL
#define BENCH_POLL_MESSAGES_PER_BATCH 8U

static ev_result_t bench_now_ms(void *context, uint32_t *out_now_ms)
{
    uint32_t *now_ms = (uint32_t *)context;
    if ((now_ms == NULL) || (out_now_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    *out_now_ms = *now_ms;
    return EV_OK;
}

static ev_result_t bench_timer_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    ev_runtime_graph_t *graph = (ev_runtime_graph_t *)context;
    return ev_runtime_graph_send(graph, target_actor, msg);
}

static void bench_build_fault_metrics_graph(ev_runtime_graph_t *graph)
{
    ev_runtime_builder_t builder;
    const ev_capability_mask_t caps = EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS;

    bench_require_ok(ev_runtime_builder_init(&builder, graph, caps, caps), "builder init poll graph");
    bench_require_ok(ev_runtime_builder_add_module(&builder, ACT_FAULT), "builder add fault");
    bench_require_ok(ev_runtime_builder_add_module(&builder, ACT_METRICS), "builder add metrics");
    bench_require_ok(ev_runtime_builder_build(&builder), "builder build poll graph");
}

static void bench_fill_fault_mailbox(ev_runtime_graph_t *graph, size_t count)
{
    size_t i;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t report;

    bench_require_ok(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP), "init fault fill publish");
    for (i = 0U; i < count; ++i) {
        bench_require_ok(ev_runtime_graph_publish(graph, &msg, &report), "fill fault mailbox");
        bench_require(report.delivered == 1U, "fault mailbox fill delivered unexpected count");
    }
}

static void bench_runtime_poll_empty(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_poll_report_t report;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t checksum = 0U;
    uint64_t i;

    bench_build_fault_metrics_graph(&graph);
    start_ns = bench_now_ns();
    for (i = 0U; i < BENCH_POLL_ITERATIONS; ++i) {
        bench_require_ok(ev_runtime_poll_once(&graph, 0U, 8U, &report), "runtime poll empty");
        checksum += report.messages_processed + report.timers_published + (uint64_t)(uint32_t)report.last_result;
    }
    elapsed_ns = bench_now_ns() - start_ns;

    bench_emit_result("runtime_poll_empty", BENCH_POLL_ITERATIONS, BENCH_POLL_ITERATIONS, elapsed_ns, checksum);
}

static void bench_runtime_poll_prefilled_mailbox(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_poll_report_t report;
    uint64_t elapsed_ns = 0U;
    uint64_t checksum = 0U;
    uint64_t i;

    bench_build_fault_metrics_graph(&graph);
    for (i = 0U; i < BENCH_POLL_BATCHES; ++i) {
        uint64_t start_ns;
        bench_fill_fault_mailbox(&graph, BENCH_POLL_MESSAGES_PER_BATCH);
        start_ns = bench_now_ns();
        bench_require_ok(ev_runtime_poll_once(&graph, 0U, BENCH_POLL_MESSAGES_PER_BATCH, &report),
                         "runtime poll prefilled mailbox");
        elapsed_ns += bench_now_ns() - start_ns;
        bench_require(report.messages_processed == BENCH_POLL_MESSAGES_PER_BATCH,
                      "runtime poll prefilled did not drain expected messages");
        checksum += report.messages_processed;
    }

    bench_emit_result("runtime_poll_prefilled_mailbox",
                      BENCH_POLL_BATCHES,
                      BENCH_POLL_BATCHES * BENCH_POLL_MESSAGES_PER_BATCH,
                      elapsed_ns,
                      checksum);
}

static void bench_runtime_loop_poll_empty(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_loop_policy_t policy;
    ev_runtime_loop_ports_t ports;
    ev_runtime_loop_report_t report;
    uint32_t now_ms = 0U;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t checksum = 0U;
    uint64_t i;

    bench_build_fault_metrics_graph(&graph);
    ev_runtime_loop_policy_default(&policy);
    policy.skip_timers = 1U;
    (void)memset(&ports, 0, sizeof(ports));
    ports.now_ms = bench_now_ms;
    ports.now_ctx = &now_ms;
    ports.timer_delivery = bench_timer_delivery;
    ports.timer_delivery_ctx = &graph;

    start_ns = bench_now_ns();
    for (i = 0U; i < BENCH_POLL_ITERATIONS; ++i) {
        bench_require_ok(ev_runtime_loop_poll_once(&graph, &policy, &ports, &report), "runtime loop empty");
        checksum += report.messages + report.timers_published + (uint64_t)(uint32_t)report.last_result;
    }
    elapsed_ns = bench_now_ns() - start_ns;

    bench_emit_result("runtime_loop_poll_empty", BENCH_POLL_ITERATIONS, BENCH_POLL_ITERATIONS, elapsed_ns, checksum);
}

static void bench_runtime_loop_poll_prefilled_mailbox(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_loop_policy_t policy;
    ev_runtime_loop_ports_t ports;
    ev_runtime_loop_report_t report;
    uint32_t now_ms = 0U;
    uint64_t elapsed_ns = 0U;
    uint64_t checksum = 0U;
    uint64_t i;

    bench_build_fault_metrics_graph(&graph);
    ev_runtime_loop_policy_default(&policy);
    policy.skip_timers = 1U;
    policy.max_messages = BENCH_POLL_MESSAGES_PER_BATCH;
    policy.scheduler_turn_budget = BENCH_POLL_MESSAGES_PER_BATCH;
    (void)memset(&ports, 0, sizeof(ports));
    ports.now_ms = bench_now_ms;
    ports.now_ctx = &now_ms;
    ports.timer_delivery = bench_timer_delivery;
    ports.timer_delivery_ctx = &graph;

    for (i = 0U; i < BENCH_POLL_BATCHES; ++i) {
        uint64_t start_ns;
        bench_fill_fault_mailbox(&graph, BENCH_POLL_MESSAGES_PER_BATCH);
        start_ns = bench_now_ns();
        bench_require_ok(ev_runtime_loop_poll_once(&graph, &policy, &ports, &report),
                         "runtime loop prefilled mailbox");
        elapsed_ns += bench_now_ns() - start_ns;
        bench_require(report.messages == BENCH_POLL_MESSAGES_PER_BATCH,
                      "runtime loop prefilled did not drain expected messages");
        checksum += report.messages;
    }

    bench_emit_result("runtime_loop_poll_prefilled_mailbox",
                      BENCH_POLL_BATCHES,
                      BENCH_POLL_BATCHES * BENCH_POLL_MESSAGES_PER_BATCH,
                      elapsed_ns,
                      checksum);
}

int main(void)
{
    bench_runtime_poll_empty();
    bench_runtime_poll_prefilled_mailbox();
    bench_runtime_loop_poll_empty();
    bench_runtime_loop_poll_prefilled_mailbox();
    return 0;
}
