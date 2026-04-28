#include "ev/fault_bus.h"

#include <string.h>

static int ev_fault_same_key(const ev_fault_payload_t *a, const ev_fault_payload_t *b)
{
    return (a->fault_id == b->fault_id) &&
           (a->source_actor == b->source_actor) &&
           (a->triggering_event == b->triggering_event);
}

void ev_fault_registry_init(ev_fault_registry_t *registry)
{
    if (registry != NULL) {
        (void)memset(registry, 0, sizeof(*registry));
    }
}

ev_result_t ev_fault_emit(ev_fault_registry_t *registry, const ev_fault_payload_t *payload)
{
    size_t i;

    if ((registry == NULL) || (payload == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    for (i = 0U; i < registry->count; ++i) {
        if (ev_fault_same_key(&registry->records[i], payload) != 0) {
            registry->records[i].counter++;
            registry->coalesced++;
            registry->emitted++;
            return EV_OK;
        }
    }

    if (registry->count >= EV_FAULT_STORE_CAPACITY) {
        registry->dropped++;
        registry->emitted++;
        return EV_ERR_FULL;
    }

    registry->records[registry->count] = *payload;
    registry->records[registry->count].counter = 1U;
    registry->count++;
    registry->emitted++;
    return EV_OK;
}

ev_result_t ev_fault_snapshot(const ev_fault_registry_t *registry, ev_fault_snapshot_t *out_snapshot)
{
    if ((registry == NULL) || (out_snapshot == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(out_snapshot, 0, sizeof(*out_snapshot));
    if (registry->count > 0U) {
        (void)memcpy(out_snapshot->records, registry->records, registry->count * sizeof(registry->records[0]));
    }
    out_snapshot->count = registry->count;
    out_snapshot->emitted = registry->emitted;
    out_snapshot->coalesced = registry->coalesced;
    out_snapshot->dropped = registry->dropped;
    return EV_OK;
}

size_t ev_fault_pending_count(const ev_fault_registry_t *registry)
{
    return (registry != NULL) ? registry->count : 0U;
}
