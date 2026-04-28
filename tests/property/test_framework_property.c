#include <assert.h>

#include "ev/mailbox.h"
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

int main(void)
{
    uint32_t seed = 0x82662026U;
    ev_msg_t storage[8];
    ev_mailbox_t mailbox;
    ev_timer_service_t timers;
    ev_trace_ring_t trace;
    size_t i;
    size_t published = 0U;

    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, 8U) == EV_OK);
    ev_timer_service_init(&timers);
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

    for (i = 0U; i < 8U; ++i) {
        ev_timer_token_t token;
        assert(ev_timer_schedule_oneshot(&timers, 100U, (uint32_t)(i + 1U), ACT_APP, EV_TICK_1S, 0U, &token) == EV_OK);
    }
    assert(ev_timer_publish_due(&timers, 200U, timer_sink, &published, 16U) == 8U);
    assert(published == 8U);
    return 0;
}
