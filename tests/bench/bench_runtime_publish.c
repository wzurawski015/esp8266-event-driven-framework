#include <stddef.h>
#include <stdint.h>

#include "bench_support.h"

#include "ev/actor_catalog.h"
#include "ev/actor_runtime.h"
#include "ev/msg.h"
#include "ev/publish.h"
#include "ev/route_table.h"
#include "ev/runtime_graph.h"

#define BENCH_PUBLISH_ITERATIONS 20000ULL
#define BENCH_PUBLISH_BATCH 8ULL

typedef struct {
    uint64_t deliveries;
    uint64_t checksum;
} bench_delivery_counter_t;

static ev_result_t bench_count_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    bench_delivery_counter_t *counter = (bench_delivery_counter_t *)context;
    if ((counter == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    counter->deliveries++;
    counter->checksum += ((uint64_t)(uint32_t)msg->event_id + 1ULL) *
                         ((uint64_t)(uint32_t)target_actor + 3ULL);
    return EV_OK;
}

static void bench_build_all_actor_graph(ev_runtime_graph_t *graph)
{
    ev_runtime_builder_t builder;
    size_t i;

    bench_require_ok(ev_runtime_builder_init(&builder, graph, EV_CAP_ALL_KNOWN, EV_CAP_ALL_KNOWN),
                     "builder init all caps");
    for (i = 0U; i < ev_actor_count(); ++i) {
        bench_require_ok(ev_runtime_builder_add_module(&builder, (ev_actor_id_t)i), "builder add module");
    }
    bench_require_ok(ev_runtime_builder_bind_routes(&builder), "builder bind routes");
    bench_require_ok(ev_runtime_builder_build(&builder), "builder build");
}

static void bench_reset_event_target_mailboxes(ev_runtime_graph_t *graph, ev_event_id_t event_id)
{
    ev_route_span_t span = ev_route_span_for_event(event_id);
    size_t i;

    for (i = 0U; i < span.count; ++i) {
        const ev_route_t *route = ev_route_at(span.start_index + i);
        ev_actor_runtime_t *runtime;
        if (route == NULL) {
            continue;
        }
        runtime = ev_runtime_graph_get_runtime(graph, route->target_actor);
        if ((runtime != NULL) && (runtime->mailbox != NULL)) {
            bench_require_ok(ev_mailbox_reset(runtime->mailbox), "reset target mailbox");
        }
    }
}

static void bench_static_publish_tick_fanout(void)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    bench_delivery_counter_t counter = {0U, 0U};
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t i;
    const ev_route_span_t span = ev_route_span_for_event(EV_TICK_1S);

    bench_require(span.count > 1U, "EV_TICK_1S must provide fanout");
    bench_require_ok(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP), "init static tick publish");

    start_ns = bench_now_ns();
    for (i = 0U; i < BENCH_PUBLISH_ITERATIONS; ++i) {
        size_t delivered = 0U;
        bench_require_ok(ev_publish(&msg, bench_count_delivery, &counter, &delivered), "static publish tick");
        bench_require(delivered == span.count, "static publish delivered unexpected fanout");
    }
    elapsed_ns = bench_now_ns() - start_ns;

    bench_emit_result("static_publish_tick_fanout",
                      BENCH_PUBLISH_ITERATIONS,
                      counter.deliveries,
                      elapsed_ns,
                      counter.checksum);
}

static void bench_static_publish_with_report(void)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    bench_delivery_counter_t counter = {0U, 0U};
    ev_publish_report_t report;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    uint64_t i;
    const ev_route_span_t span = ev_route_span_for_event(EV_TICK_1S);

    bench_require(span.count > 1U, "EV_TICK_1S must provide fanout for report benchmark");
    bench_require_ok(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP), "init static report publish");

    start_ns = bench_now_ns();
    for (i = 0U; i < BENCH_PUBLISH_ITERATIONS; ++i) {
        bench_require_ok(ev_publish_ex(&msg,
                                       bench_count_delivery,
                                       &counter,
                                       EV_PUBLISH_FAIL_FAST,
                                       &report),
                         "static publish report");
        bench_require(report.delivered_count == span.count, "static report delivered unexpected fanout");
    }
    elapsed_ns = bench_now_ns() - start_ns;

    bench_emit_result("static_publish_with_report",
                      BENCH_PUBLISH_ITERATIONS,
                      counter.deliveries,
                      elapsed_ns,
                      counter.checksum + (uint64_t)report.delivered_count);
}

static void bench_active_publish_tick_fanout(void)
{
    ev_runtime_graph_t graph;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    const ev_route_span_t span = ev_route_span_for_event(EV_TICK_1S);
    uint64_t iterations = 0U;
    uint64_t elapsed_ns = 0U;
    uint64_t checksum = 0U;

    bench_require(span.count > 1U, "EV_TICK_1S active fanout route missing");
    bench_build_all_actor_graph(&graph);
    bench_require_ok(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP), "init active tick publish");

    while (iterations < BENCH_PUBLISH_ITERATIONS) {
        uint64_t batch = 0U;
        uint64_t start_ns = bench_now_ns();
        while ((batch < BENCH_PUBLISH_BATCH) && (iterations < BENCH_PUBLISH_ITERATIONS)) {
            bench_require_ok(ev_runtime_graph_publish(&graph, &msg, NULL), "active tick publish");
            checksum += span.count;
            batch++;
            iterations++;
        }
        elapsed_ns += bench_now_ns() - start_ns;
        bench_reset_event_target_mailboxes(&graph, EV_TICK_1S);
    }

    bench_emit_result("active_publish_tick_fanout", iterations, iterations * span.count, elapsed_ns, checksum);
}

static void bench_active_publish_fault_single(void)
{
    ev_runtime_graph_t graph;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    const ev_route_span_t span = ev_route_span_for_event(EV_FAULT_REPORTED);
    uint64_t iterations = 0U;
    uint64_t elapsed_ns = 0U;
    uint64_t checksum = 0U;

    bench_require(span.count == 1U, "EV_FAULT_REPORTED must have one target route");
    bench_build_all_actor_graph(&graph);
    bench_require_ok(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP), "init active fault publish");

    while (iterations < BENCH_PUBLISH_ITERATIONS) {
        uint64_t batch = 0U;
        uint64_t start_ns = bench_now_ns();
        while ((batch < BENCH_PUBLISH_BATCH) && (iterations < BENCH_PUBLISH_ITERATIONS)) {
            bench_require_ok(ev_runtime_graph_publish(&graph, &msg, NULL), "active fault publish");
            checksum += span.count;
            batch++;
            iterations++;
        }
        elapsed_ns += bench_now_ns() - start_ns;
        bench_reset_event_target_mailboxes(&graph, EV_FAULT_REPORTED);
    }

    bench_emit_result("active_publish_fault_single", iterations, iterations * span.count, elapsed_ns, checksum);
}

static void bench_active_publish_with_delivery_report(void)
{
    ev_runtime_graph_t graph;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_delivery_report_t report;
    const ev_route_span_t span = ev_route_span_for_event(EV_FAULT_REPORTED);
    uint64_t iterations = 0U;
    uint64_t elapsed_ns = 0U;
    uint64_t checksum = 0U;

    bench_require(span.count == 1U, "EV_FAULT_REPORTED must have one target route for report benchmark");
    bench_build_all_actor_graph(&graph);
    bench_require_ok(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP), "init active report publish");

    while (iterations < BENCH_PUBLISH_ITERATIONS) {
        uint64_t batch = 0U;
        uint64_t start_ns = bench_now_ns();
        while ((batch < BENCH_PUBLISH_BATCH) && (iterations < BENCH_PUBLISH_ITERATIONS)) {
            bench_require_ok(ev_runtime_graph_publish(&graph, &msg, &report), "active report publish");
            bench_require(report.delivered == 1U, "active report delivered unexpected count");
            checksum += report.matched_routes + report.delivered;
            batch++;
            iterations++;
        }
        elapsed_ns += bench_now_ns() - start_ns;
        bench_reset_event_target_mailboxes(&graph, EV_FAULT_REPORTED);
    }

    bench_emit_result("active_publish_with_delivery_report", iterations, iterations * span.count, elapsed_ns, checksum);
}

int main(void)
{
    bench_static_publish_tick_fanout();
    bench_static_publish_with_report();
    bench_active_publish_tick_fanout();
    bench_active_publish_fault_single();
    bench_active_publish_with_delivery_report();
    return 0;
}
