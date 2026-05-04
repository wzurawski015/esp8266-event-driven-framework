#include <assert.h>
#include <stdint.h>

#include "ev/network_outbox.h"

int main(void)
{
    ev_network_outbox_t outbox;
    ev_network_outbox_item_t item;
    ev_network_outbox_stats_t stats;
    ev_network_backpressure_policy_t policy = {{EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY, EV_NETWORK_OUTBOX_CAPACITY}, {1U, 1U, 1U, 1U}};
    const uint8_t payload[2] = {1U, 2U};
    size_t i;

    ev_network_outbox_init(&outbox);
    for (i = 0U; i < EV_NETWORK_OUTBOX_CAPACITY; ++i) {
        assert(ev_network_outbox_push(&outbox, &policy, EV_NETWORK_MSG_TELEMETRY_PERIODIC, payload, sizeof(payload)) == EV_OK);
    }
    assert(ev_network_outbox_pending(&outbox) == EV_NETWORK_OUTBOX_CAPACITY);
    assert(outbox.high_water == EV_NETWORK_OUTBOX_CAPACITY);
    assert(ev_network_outbox_push(&outbox, &policy, EV_NETWORK_MSG_COMMAND_RESPONSE, payload, sizeof(payload)) == EV_ERR_FULL);
    assert(outbox.rejected == 1U);
    for (i = 0U; i < EV_NETWORK_OUTBOX_CAPACITY; ++i) {
        assert(ev_network_outbox_pop(&outbox, &item) == EV_OK);
    }
    assert(ev_network_outbox_pop(&outbox, &item) == EV_ERR_EMPTY);

    outbox.write_seq = UINT32_MAX - 2U;
    outbox.read_seq = UINT32_MAX - 2U;
    for (i = 0U; i < 4U; ++i) {
        assert(ev_network_outbox_push(&outbox, &policy, EV_NETWORK_MSG_FAULT_CRITICAL, payload, sizeof(payload)) == EV_OK);
    }
    assert(ev_network_outbox_pending(&outbox) == 4U);
    assert(ev_network_outbox_stats(&outbox, &stats) == EV_OK);
    assert(stats.pending == 4U);
    for (i = 0U; i < 4U; ++i) {
        assert(ev_network_outbox_pop(&outbox, &item) == EV_OK);
        assert(item.category == EV_NETWORK_MSG_FAULT_CRITICAL);
    }
    return 0;
}
