#ifndef EV_INGRESS_SERVICE_H
#define EV_INGRESS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "ev/msg.h"
#include "ev/result.h"

#ifndef EV_INGRESS_CAPACITY
#define EV_INGRESS_CAPACITY 16U
#endif
#define EV_INGRESS_MASK (EV_INGRESS_CAPACITY - 1U)

typedef struct {
    ev_msg_t queue[EV_INGRESS_CAPACITY];
    uint32_t write_seq;
    uint32_t read_seq;
    uint32_t high_water;
    uint32_t pushed;
    uint32_t dropped;
} ev_ingress_service_t;

void ev_ingress_service_init(ev_ingress_service_t *ingress);
ev_result_t ev_ingress_push(ev_ingress_service_t *ingress, const ev_msg_t *msg);
ev_result_t ev_ingress_pop(ev_ingress_service_t *ingress, ev_msg_t *out_msg);
size_t ev_ingress_pending(const ev_ingress_service_t *ingress);

#endif
