#include "ev/trace_ring.h"

#include <string.h>

void ev_trace_ring_init(ev_trace_ring_t *ring)
{
    if (ring != NULL) {
        (void)memset(ring, 0, sizeof(*ring));
    }
}

ev_result_t ev_trace_record(ev_trace_ring_t *ring, const ev_trace_record_t *record)
{
#if EV_TRACE_ENABLE
    size_t index;

    if ((ring == NULL) || (record == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    index = (ring->head + ring->count) % EV_TRACE_RING_CAPACITY;
    if (ring->count == EV_TRACE_RING_CAPACITY) {
        index = ring->head;
        ring->head = (ring->head + 1U) % EV_TRACE_RING_CAPACITY;
        ring->dropped++;
    } else {
        ring->count++;
    }
    ring->records[index] = *record;
    return EV_OK;
#else
    (void)ring;
    (void)record;
    return EV_OK;
#endif
}

size_t ev_trace_pending(const ev_trace_ring_t *ring)
{
    return (ring != NULL) ? ring->count : 0U;
}

uint32_t ev_trace_dropped(const ev_trace_ring_t *ring)
{
    return (ring != NULL) ? ring->dropped : 0U;
}


size_t ev_trace_drain(ev_trace_ring_t *ring, ev_trace_record_t *out_records, size_t max_records)
{
#if EV_TRACE_ENABLE
    size_t drained = 0U;
    if ((ring == NULL) || ((out_records == NULL) && (max_records > 0U))) {
        return 0U;
    }
    while ((drained < max_records) && (ring->count > 0U)) {
        out_records[drained] = ring->records[ring->head];
        ring->head = (ring->head + 1U) % EV_TRACE_RING_CAPACITY;
        ring->count--;
        drained++;
    }
    return drained;
#else
    (void)ring;
    (void)out_records;
    (void)max_records;
    return 0U;
#endif
}

void ev_trace_clear(ev_trace_ring_t *ring)
{
    if (ring != NULL) {
        ring->head = 0U;
        ring->count = 0U;
    }
}
