#include "ev/ingress_service.h"

#include <string.h>

#include "ev/compiler.h"

EV_STATIC_ASSERT((EV_INGRESS_CAPACITY != 0U) && ((EV_INGRESS_CAPACITY & EV_INGRESS_MASK) == 0U), "EV_INGRESS_CAPACITY must be a power of two");

static uint32_t ev_ingress_pending_u32(const ev_ingress_service_t *ingress)
{
    return (ingress != NULL) ? (uint32_t)(ingress->write_seq - ingress->read_seq) : 0U;
}

void ev_ingress_service_init(ev_ingress_service_t *ingress)
{
    if (ingress != NULL) {
        (void)memset(ingress, 0, sizeof(*ingress));
    }
}

ev_result_t ev_ingress_push(ev_ingress_service_t *ingress, const ev_msg_t *msg)
{
    uint32_t pending;
    uint32_t index;

    if ((ingress == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    pending = ev_ingress_pending_u32(ingress);
    if (pending >= EV_INGRESS_CAPACITY) {
        ingress->dropped++;
        return EV_ERR_FULL;
    }
    index = ingress->write_seq & EV_INGRESS_MASK;
    ingress->queue[index] = *msg;
    ingress->write_seq++;
    ingress->pushed++;
    pending++;
    if (pending > ingress->high_water) {
        ingress->high_water = pending;
    }
    return EV_OK;
}

ev_result_t ev_ingress_pop(ev_ingress_service_t *ingress, ev_msg_t *out_msg)
{
    uint32_t index;

    if ((ingress == NULL) || (out_msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_ingress_pending_u32(ingress) == 0U) {
        return EV_ERR_EMPTY;
    }
    index = ingress->read_seq & EV_INGRESS_MASK;
    *out_msg = ingress->queue[index];
    ingress->read_seq++;
    return EV_OK;
}

size_t ev_ingress_pending(const ev_ingress_service_t *ingress)
{
    return (size_t)ev_ingress_pending_u32(ingress);
}
