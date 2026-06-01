#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "ev/dispose.h"
#include "ev/lease_pool.h"
#include "ev/mailbox.h"
#include "ev/msg.h"
#include "ev/power_state_machine.h"
#include "ev/qos_contract.h"

static uint32_t rng_state = 0x5eed2026u;

static uint32_t next_u32(void)
{
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

static void exercise_msg_contract(uint32_t r)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    unsigned char bytes[4] = { (unsigned char)r, (unsigned char)(r >> 8), (unsigned char)(r >> 16), (unsigned char)(r >> 24) };
    if ((r & 1u) == 0u) {
        assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_BOOT) == EV_OK);
        assert(ev_msg_set_inline_payload(&msg, bytes, (size_t)(r % 5u)) == EV_OK);
    } else {
        assert(ev_msg_init_send(&msg, EV_DIAG_SNAPSHOT_REQ, ACT_APP, ACT_DIAG) == EV_OK);
    }
    assert(ev_msg_validate_payload_contract(&msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void exercise_mailbox_contract(uint32_t r)
{
    ev_msg_t storage[8] = {{0}};
    ev_mailbox_t mailbox;
    ev_msg_t out = EV_MSG_INITIALIZER;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    size_t n = (size_t)(r % 12u);
    size_t i;
    assert(ev_mailbox_init(&mailbox, EV_MAILBOX_FIFO_8, storage, 8U) == EV_OK);
    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_BOOT) == EV_OK);
    for (i = 0; i < n; ++i) {
        ev_route_qos_t qos = (i & 1U) ? EV_ROUTE_QOS_LATEST_ONLY : EV_ROUTE_QOS_COALESCED;
        (void)ev_mailbox_push_qos(&mailbox, &msg, qos, NULL);
        assert(ev_mailbox_count(&mailbox) <= ev_mailbox_capacity(&mailbox));
    }
    while (ev_mailbox_pop(&mailbox, &out) == EV_OK) {
        assert(out.event_id == EV_TICK_1S);
        assert(ev_msg_dispose(&out) == EV_OK);
    }
    assert(ev_mailbox_reset(&mailbox) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void exercise_power_contract(uint32_t r)
{
    ev_power_state_machine_t sm;
    ev_power_transition_input_t ok = {EV_OK, 0U};
    ev_power_transition_input_t bad = {EV_ERR_STATE, 1U};
    ev_power_state_machine_init(&sm);
    if ((r % 3u) == 0u) {
        (void)ev_power_state_machine_step(&sm, EV_POWER_ACTION_DEEP_SLEEP_ENTERED, &bad, NULL);
        assert(sm.state == EV_POWER_STATE_ACTIVE || sm.state == EV_POWER_STATE_FAILED);
    } else {
        assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_REQUEST_SLEEP, &ok, NULL) == EV_OK);
        if ((r & 1u) != 0u) {
            assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DRAIN_REJECTED, &bad, NULL) == EV_OK);
            assert(sm.state == EV_POWER_STATE_REJECTED);
        } else {
            assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DRAIN_COMPLETE, &ok, NULL) == EV_OK);
        }
    }
}

static void exercise_qos_contract(uint32_t r)
{
    ev_qos_contract_t c;
    ev_route_qos_t qos = (ev_route_qos_t)(r % (EV_ROUTE_QOS_COMMAND + 2));
    ev_result_t res = ev_qos_contract_for(qos, &c);
    if (qos <= EV_ROUTE_QOS_COMMAND) {
        assert(res == EV_OK);
        assert(c.qos == qos);
        if (qos == EV_ROUTE_QOS_COALESCED) {
            assert(c.allows_coalesce != 0U);
            assert(c.failure_behavior == EV_QOS_FAILURE_COALESCE);
        }
        if (qos == EV_ROUTE_QOS_LATEST_ONLY) {
            assert(c.allows_latest_replace != 0U);
            assert(c.failure_behavior == EV_QOS_FAILURE_REPLACE_LATEST);
        }
    } else {
        assert(res == EV_ERR_OUT_OF_RANGE);
    }
}

int main(void)
{
    unsigned i;
    for (i = 0U; i < 50000U; ++i) {
        uint32_t r = next_u32();
        switch (r & 3U) {
        case 0U: exercise_msg_contract(r); break;
        case 1U: exercise_mailbox_contract(r); break;
        case 2U: exercise_power_contract(r); break;
        default: exercise_qos_contract(r); break;
        }
    }
    return 0;
}
