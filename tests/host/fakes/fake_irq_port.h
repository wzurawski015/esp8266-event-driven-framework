#ifndef TESTS_HOST_FAKES_FAKE_IRQ_PORT_H
#define TESTS_HOST_FAKES_FAKE_IRQ_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/port_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_IRQ_PORT_CAPACITY 32U

typedef struct {
    ev_irq_sample_t ring[FAKE_IRQ_PORT_CAPACITY];
    size_t head;
    size_t tail;
    size_t count;
    size_t high_watermark;
    bool enabled[8];
    uint32_t pop_calls;
    uint32_t enable_calls;
    uint32_t wait_calls;
    uint32_t get_stats_calls;
    uint32_t write_seq;
    uint32_t read_seq;
    uint32_t dropped_samples;
} fake_irq_port_t;

void fake_irq_port_init(fake_irq_port_t *fake);
void fake_irq_port_bind(ev_irq_port_t *out_port, fake_irq_port_t *fake);
ev_result_t fake_irq_port_push(fake_irq_port_t *fake, const ev_irq_sample_t *sample);
bool fake_irq_port_is_enabled(const fake_irq_port_t *fake, ev_irq_line_id_t line_id);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_IRQ_PORT_H */
