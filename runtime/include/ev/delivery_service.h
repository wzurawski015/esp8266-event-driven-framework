#ifndef EV_DELIVERY_SERVICE_H
#define EV_DELIVERY_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "ev/msg.h"
#include "ev/result.h"
#include "ev/route_table.h"

struct ev_runtime_graph;

typedef struct {
    struct ev_runtime_graph *graph;
} ev_delivery_service_t;

typedef struct {
    size_t matched_routes;
    size_t attempted;
    size_t delivered;
    size_t dropped;
    ev_result_t first_error;
    ev_actor_id_t first_failed_actor;
} ev_delivery_report_t;

void ev_delivery_service_init(ev_delivery_service_t *svc, struct ev_runtime_graph *graph);
void ev_delivery_report_reset(ev_delivery_report_t *report);
ev_result_t ev_delivery_publish(ev_delivery_service_t *svc, const ev_msg_t *msg, ev_delivery_report_t *report);

#endif
