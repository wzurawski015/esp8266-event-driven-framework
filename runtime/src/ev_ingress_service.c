#include "ev/ingress_service.h"

#include <string.h>

void ev_ingress_service_init(ev_ingress_service_t *ingress)
{
    if (ingress != NULL) {
        (void)memset(ingress, 0, sizeof(*ingress));
    }
}

ev_result_t ev_ingress_push(ev_ingress_service_t *ingress, const ev_msg_t *msg)
{
    size_t index;

    if ((ingress == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ingress->count >= EV_INGRESS_CAPACITY) {
        ingress->dropped++;
        return EV_ERR_FULL;
    }
    index = (ingress->head + ingress->count) % EV_INGRESS_CAPACITY;
    ingress->queue[index] = *msg;
    ingress->count++;
    ingress->pushed++;
    return EV_OK;
}

ev_result_t ev_ingress_pop(ev_ingress_service_t *ingress, ev_msg_t *out_msg)
{
    if ((ingress == NULL) || (out_msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ingress->count == 0U) {
        return EV_ERR_EMPTY;
    }
    *out_msg = ingress->queue[ingress->head];
    ingress->head = (ingress->head + 1U) % EV_INGRESS_CAPACITY;
    ingress->count--;
    return EV_OK;
}

size_t ev_ingress_pending(const ev_ingress_service_t *ingress)
{
    return (ingress != NULL) ? ingress->count : 0U;
}
