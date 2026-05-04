#include <assert.h>
#include <stdint.h>

#include "ev/ingress_service.h"
#include "ev/mailbox.h"
#include "ev/network_outbox.h"
#include "ev/runtime_graph.h"
#include "ev/timer_service.h"
#include "ev/trace_ring.h"

static uint32_t xorshift32(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static ev_result_t timer_sink(ev_actor_id_t actor, const ev_msg_t *msg, void *ctx)
{
    size_t *count = (size_t *)ctx;
    (void)actor;
    (void)msg;
    (*count)++;
    return EV_OK;
}

static void property_mailbox_trace(void)
{
    uint32_t seed = 0x82662026U;
    ev_msg_t storage[8];
    ev_mailbox_t mailbox;
    ev_trace_ring_t trace;
    size_t i;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, 8U) == EV_OK);
    ev_trace_ring_init(&trace);

    for (i = 0U; i < 1000U; ++i) {
        uint32_t r = xorshift32(&seed);
        ev_msg_t msg = EV_MSG_INITIALIZER;
        assert(ev_msg_init_send(&msg, (r & 1U) ? EV_TICK_1S : EV_TEMP_UPDATED, ACT_APP, ACT_DIAG) == EV_OK);
        if ((r & 3U) == 0U) {
            (void)ev_mailbox_push(&mailbox, &msg);
        } else if ((r & 3U) == 1U) {
            (void)ev_mailbox_pop(&mailbox, &msg);
        } else {
            ev_trace_record_t rec = {r, msg.event_id, ACT_APP, ACT_DIAG, EV_OK, EV_ROUTE_QOS_TELEMETRY, (uint16_t)ev_mailbox_count(&mailbox), 0U};
            (void)ev_trace_record(&trace, &rec);
        }
        assert(ev_mailbox_count(&mailbox) <= ev_mailbox_capacity(&mailbox));
    }
}

static void property_timer_ordering(void)
{
    ev_timer_service_t timers;
    size_t i;
    size_t published = 0U;
    ev_timer_service_init(&timers);
    for (i = 0U; i < 8U; ++i) {
        ev_timer_token_t token;
        assert(ev_timer_schedule_oneshot(&timers, 100U, (uint32_t)(i + 1U), ACT_APP, EV_TICK_1S, 0U, &token) == EV_OK);
    }
    assert(ev_timer_publish_due(&timers, 200U, timer_sink, &published, 16U) == 8U);
    assert(published == 8U);
}

static void property_sequence_rings(void)
{
    uint32_t seed = 0x5E10EACEU;
    ev_ingress_service_t ingress;
    ev_network_outbox_t outbox;
    ev_network_backpressure_policy_t policy = {{EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY}, {1U, 1U, 1U, 1U}};
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_msg_t out_msg = EV_MSG_INITIALIZER;
    uint8_t payload[4] = {1U, 2U, 3U, 4U};
    size_t i;

    ev_ingress_service_init(&ingress);
    ev_network_outbox_init(&outbox);
    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP) == EV_OK);
    ingress.write_seq = UINT32_MAX - 16U;
    ingress.read_seq = UINT32_MAX - 16U;
    outbox.write_seq = UINT32_MAX - 16U;
    outbox.read_seq = UINT32_MAX - 16U;

    for (i = 0U; i < 512U; ++i) {
        uint32_t r = xorshift32(&seed);
        if ((r & 1U) != 0U) {
            (void)ev_ingress_push(&ingress, &msg);
            (void)ev_network_outbox_push(&outbox, &policy, (ev_network_msg_category_t)(r % EV_NETWORK_MSG_CATEGORY_COUNT), payload, sizeof(payload));
        } else {
            ev_network_outbox_item_t item;
            (void)ev_ingress_pop(&ingress, &out_msg);
            (void)ev_network_outbox_pop(&outbox, &item);
        }
        assert(ev_ingress_pending(&ingress) <= EV_INGRESS_CAPACITY);
        assert(ev_network_outbox_pending(&outbox) <= EV_NETWORK_OUTBOX_CAPACITY);
    }
}

static void property_route_validation(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    const ev_active_route_table_t *routes;
    assert(ev_runtime_builder_init(&builder, &graph, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS, EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    routes = ev_runtime_graph_active_routes(&graph);
    assert(routes != 0);
    assert(routes->optional_disabled_count > 0U);
}

int main(void)
{
    property_mailbox_trace();
    property_timer_ordering();
    property_sequence_rings();
    property_route_validation();
    return 0;
}
