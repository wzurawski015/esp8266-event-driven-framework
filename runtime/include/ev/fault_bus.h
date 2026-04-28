#ifndef EV_FAULT_BUS_H
#define EV_FAULT_BUS_H

#include <stddef.h>
#include <stdint.h>
#include "ev/actor_id.h"
#include "ev/event_id.h"
#include "ev/result.h"

#ifndef EV_FAULT_STORE_CAPACITY
#define EV_FAULT_STORE_CAPACITY 16U
#endif

typedef enum {
#define EV_FAULT(name, code_hex, summary) name = code_hex,
#include "faults.def"
#undef EV_FAULT
} ev_fault_id_t;

typedef enum {
    EV_FAULT_SEV_INFO = 0,
    EV_FAULT_SEV_WARNING = 1,
    EV_FAULT_SEV_ERROR = 2,
    EV_FAULT_SEV_CRITICAL = 3
} ev_fault_severity_t;

typedef struct {
    ev_fault_id_t fault_id;
    ev_fault_severity_t severity;
    ev_actor_id_t source_actor;
    ev_event_id_t triggering_event;
    uint32_t source_module;
    uint32_t timestamp_ms;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t counter;
    uint32_t flags;
} ev_fault_payload_t;

typedef struct {
    ev_fault_payload_t records[EV_FAULT_STORE_CAPACITY];
    size_t count;
    uint32_t emitted;
    uint32_t coalesced;
    uint32_t dropped;
} ev_fault_registry_t;

typedef struct {
    ev_fault_payload_t records[EV_FAULT_STORE_CAPACITY];
    size_t count;
    uint32_t emitted;
    uint32_t coalesced;
    uint32_t dropped;
} ev_fault_snapshot_t;

void ev_fault_registry_init(ev_fault_registry_t *registry);
ev_result_t ev_fault_emit(ev_fault_registry_t *registry, const ev_fault_payload_t *payload);
ev_result_t ev_fault_snapshot(const ev_fault_registry_t *registry, ev_fault_snapshot_t *out_snapshot);
size_t ev_fault_pending_count(const ev_fault_registry_t *registry);

#endif
