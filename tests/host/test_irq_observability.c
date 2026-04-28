#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/port_irq.h"
#include "fakes/fake_irq_port.h"

int main(void)
{
    fake_irq_port_t fake;
    ev_irq_port_t port = {0};
    ev_irq_stats_t stats = {0};
    ev_irq_sample_t sample = {
        .line_id = 0U,
        .edge = EV_IRQ_EDGE_FALLING,
        .level = 0U,
        .timestamp_us = 1U,
    };
    ev_irq_sample_t popped = {0};
    size_t i;

    fake_irq_port_init(&fake);
    fake_irq_port_bind(&port, &fake);

    assert(port.get_stats != NULL);
    assert(port.get_stats(port.ctx, &stats) == EV_OK);
    assert(stats.pending_samples == 0U);
    assert(stats.dropped_samples == 0U);
    assert(stats.high_watermark == 0U);

    for (i = 0U; i < 3U; ++i) {
        sample.timestamp_us = (uint32_t)(10U + i);
        assert(fake_irq_port_push(&fake, &sample) == EV_OK);
    }

    assert(port.get_stats(port.ctx, &stats) == EV_OK);
    assert(stats.write_seq == 3U);
    assert(stats.read_seq == 0U);
    assert(stats.pending_samples == 3U);
    assert(stats.high_watermark == 3U);

    assert(port.pop(port.ctx, &popped) == EV_OK);
    assert(port.get_stats(port.ctx, &stats) == EV_OK);
    assert(stats.pending_samples == 2U);
    assert(stats.read_seq == 1U);
    assert(stats.high_watermark == 3U);

    while (fake.count < FAKE_IRQ_PORT_CAPACITY) {
        assert(fake_irq_port_push(&fake, &sample) == EV_OK);
    }
    assert(fake_irq_port_push(&fake, &sample) == EV_ERR_FULL);
    assert(port.get_stats(port.ctx, &stats) == EV_OK);
    assert(stats.dropped_samples == 1U);
    assert(stats.high_watermark == FAKE_IRQ_PORT_CAPACITY);

    return 0;
}
