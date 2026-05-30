#include <assert.h>
#include <string.h>

#include "ev/msg.h"
#include "ev/runtime_graph.h"
#include "fakes/fake_clock_port.h"

static void build_fault_graph(ev_runtime_graph_t *graph, ev_runtime_builder_t *builder, ev_clock_port_t *clock_port)
{
    ev_runtime_ports_t ports;
    ev_capability_mask_t caps = EV_CAP_FAULTS | EV_CAP_TRACE;

    assert(ev_runtime_builder_init(builder, graph, caps, caps) == EV_OK);
    if (clock_port != NULL) {
        memset(&ports, 0, sizeof(ports));
        ports.clock = clock_port;
        assert(ev_runtime_builder_set_ports(builder, &ports) == EV_OK);
    }
    assert(ev_runtime_builder_add_module(builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_bind_routes(builder) == EV_OK);
    assert(ev_runtime_builder_build(builder) == EV_OK);
}

static ev_trace_record_t publish_fault_and_drain_trace(ev_runtime_graph_t *graph, ev_delivery_report_t *out_report)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_trace_record_t rec;

    memset(&rec, 0, sizeof(rec));
    assert(ev_msg_init_publish(&msg, EV_FAULT_REPORTED, ACT_APP) == EV_OK);
    assert(ev_runtime_graph_publish(graph, &msg, out_report) == EV_OK);
    assert(out_report->matched_routes == 1U);
    assert(out_report->delivered == 1U);
    assert(ev_runtime_graph_trace_drain(graph, &rec, 1U) == 1U);
    assert(rec.event_id == EV_FAULT_REPORTED);
    assert(rec.source_actor == ACT_APP);
    assert(rec.target_actor == ACT_FAULT);
    assert(rec.result == EV_OK);
    return rec;
}

static void trace_uses_monotonic_clock_port(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    fake_clock_port_t fake_clock;
    ev_clock_port_t clock_port;
    ev_delivery_report_t report;
    ev_trace_record_t first;
    ev_trace_record_t second;

    fake_clock_port_init(&fake_clock);
    fake_clock.mono_now_us = 1000U;
    fake_clock.mono_step_us = 25U;
    fake_clock_port_bind(&clock_port, &fake_clock);

    build_fault_graph(&graph, &builder, &clock_port);

    first = publish_fault_and_drain_trace(&graph, &report);
    second = publish_fault_and_drain_trace(&graph, &report);

    assert(first.timestamp_us == 1000U);
    assert(second.timestamp_us == 1025U);
    assert(second.timestamp_us > first.timestamp_us);
    assert(fake_clock.mono_calls == 2U);
}

static void trace_stores_low_32_bits_of_monotonic_microseconds(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    fake_clock_port_t fake_clock;
    ev_clock_port_t clock_port;
    ev_delivery_report_t report;
    ev_trace_record_t rec;

    fake_clock_port_init(&fake_clock);
    fake_clock.mono_now_us = 0x100000123ULL;
    fake_clock_port_bind(&clock_port, &fake_clock);

    build_fault_graph(&graph, &builder, &clock_port);
    rec = publish_fault_and_drain_trace(&graph, &report);

    assert(rec.timestamp_us == 0x123U);
}

static void trace_falls_back_to_zero_without_clock_port(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_delivery_report_t report;
    ev_trace_record_t rec;

    build_fault_graph(&graph, &builder, NULL);
    rec = publish_fault_and_drain_trace(&graph, &report);

    assert(rec.timestamp_us == 0U);
}

static void trace_falls_back_to_zero_when_clock_fails_without_blocking_delivery(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    fake_clock_port_t fake_clock;
    ev_clock_port_t clock_port;
    ev_delivery_report_t report;
    ev_trace_record_t rec;

    fake_clock_port_init(&fake_clock);
    fake_clock.mono_now_us = 5000U;
    fake_clock.next_mono_result = EV_ERR_TIMEOUT;
    fake_clock_port_bind(&clock_port, &fake_clock);

    build_fault_graph(&graph, &builder, &clock_port);
    rec = publish_fault_and_drain_trace(&graph, &report);

    assert(rec.timestamp_us == 0U);
    assert(report.delivered == 1U);
    assert(fake_clock.mono_calls == 1U);
}

int main(void)
{
    trace_uses_monotonic_clock_port();
    trace_stores_low_32_bits_of_monotonic_microseconds();
    trace_falls_back_to_zero_without_clock_port();
    trace_falls_back_to_zero_when_clock_fails_without_blocking_delivery();
    return 0;
}
