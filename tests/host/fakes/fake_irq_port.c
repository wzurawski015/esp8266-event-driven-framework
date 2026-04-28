#include "fake_irq_port.h"

#include <string.h>

static void fake_irq_record_high_watermark(fake_irq_port_t *fake)
{
    if ((fake != NULL) && (fake->count > fake->high_watermark)) {
        fake->high_watermark = fake->count;
    }
}

static ev_result_t fake_irq_pop(void *ctx, ev_irq_sample_t *out_sample)
{
    fake_irq_port_t *fake = (fake_irq_port_t *)ctx;

    if ((fake == NULL) || (out_sample == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->pop_calls;
    if (fake->count == 0U) {
        return EV_ERR_EMPTY;
    }

    *out_sample = fake->ring[fake->head];
    fake->head = (fake->head + 1U) % FAKE_IRQ_PORT_CAPACITY;
    --fake->count;
    ++fake->read_seq;
    return EV_OK;
}

static ev_result_t fake_irq_wait(void *ctx, uint32_t timeout_ms, bool *out_woken)
{
    fake_irq_port_t *fake = (fake_irq_port_t *)ctx;

    if ((fake == NULL) || (out_woken == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    (void)timeout_ms;
    ++fake->wait_calls;
    *out_woken = (fake->count > 0U);
    return EV_OK;
}

static ev_result_t fake_irq_get_stats(void *ctx, ev_irq_stats_t *out_stats)
{
    fake_irq_port_t *fake = (fake_irq_port_t *)ctx;

    if ((fake == NULL) || (out_stats == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->get_stats_calls;
    out_stats->write_seq = fake->write_seq;
    out_stats->read_seq = fake->read_seq;
    out_stats->pending_samples = (uint32_t)fake->count;
    out_stats->dropped_samples = fake->dropped_samples;
    out_stats->high_watermark = (uint32_t)fake->high_watermark;
    return EV_OK;
}

static ev_result_t fake_irq_enable(void *ctx, ev_irq_line_id_t line_id, bool enabled)
{
    fake_irq_port_t *fake = (fake_irq_port_t *)ctx;

    if ((fake == NULL) || (line_id >= (ev_irq_line_id_t)(sizeof(fake->enabled) / sizeof(fake->enabled[0])))) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->enable_calls;
    fake->enabled[line_id] = enabled;
    return EV_OK;
}

void fake_irq_port_init(fake_irq_port_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
    }
}

void fake_irq_port_bind(ev_irq_port_t *out_port, fake_irq_port_t *fake)
{
    if (out_port != NULL) {
        memset(out_port, 0, sizeof(*out_port));
        out_port->ctx = fake;
        out_port->pop = fake_irq_pop;
        out_port->enable = fake_irq_enable;
        out_port->wait = fake_irq_wait;
        out_port->get_stats = fake_irq_get_stats;
    }
}

ev_result_t fake_irq_port_push(fake_irq_port_t *fake, const ev_irq_sample_t *sample)
{
    if ((fake == NULL) || (sample == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    if (fake->count >= FAKE_IRQ_PORT_CAPACITY) {
        ++fake->dropped_samples;
        return EV_ERR_FULL;
    }

    fake->ring[fake->tail] = *sample;
    fake->tail = (fake->tail + 1U) % FAKE_IRQ_PORT_CAPACITY;
    ++fake->count;
    ++fake->write_seq;
    fake_irq_record_high_watermark(fake);
    return EV_OK;
}

bool fake_irq_port_is_enabled(const fake_irq_port_t *fake, ev_irq_line_id_t line_id)
{
    if ((fake == NULL) || (line_id >= (ev_irq_line_id_t)(sizeof(fake->enabled) / sizeof(fake->enabled[0])))) {
        return false;
    }

    return fake->enabled[line_id];
}
