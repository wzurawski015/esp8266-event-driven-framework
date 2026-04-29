#ifndef EV_TRACE_RING_H
#define EV_TRACE_RING_H

#include <stddef.h>
#include <stdint.h>
#include "ev/actor_id.h"
#include "ev/event_id.h"
#include "ev/result.h"
#include "ev/route_table.h"

#ifndef EV_TRACE_ENABLE
#define EV_TRACE_ENABLE 1
#endif

#ifndef EV_TRACE_RING_CAPACITY
#define EV_TRACE_RING_CAPACITY 32U
#endif

typedef struct {
    uint32_t timestamp_us;
    ev_event_id_t event_id;
    ev_actor_id_t source_actor;
    ev_actor_id_t target_actor;
    ev_result_t result;
    ev_route_qos_t qos;
    uint16_t queue_depth;
    uint32_t flags;
} ev_trace_record_t;

typedef struct {
    ev_trace_record_t records[EV_TRACE_RING_CAPACITY];
    size_t head;
    size_t count;
    uint32_t dropped;
} ev_trace_ring_t;

void ev_trace_ring_init(ev_trace_ring_t *ring);
ev_result_t ev_trace_record(ev_trace_ring_t *ring, const ev_trace_record_t *record);
size_t ev_trace_pending(const ev_trace_ring_t *ring);
uint32_t ev_trace_dropped(const ev_trace_ring_t *ring);
size_t ev_trace_drain(ev_trace_ring_t *ring, ev_trace_record_t *out_records, size_t max_records);
void ev_trace_clear(ev_trace_ring_t *ring);

#endif
