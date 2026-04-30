#ifndef EV_ACTOR_PUBLISH_PORT_H
#define EV_ACTOR_PUBLISH_PORT_H

#include <stdint.h>

#include "ev/delivery.h"
#include "ev/delivery_service.h"
#include "ev/runtime_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t sends_attempted;
    uint32_t sends_ok;
    uint32_t optional_disabled_routes;
    uint32_t failed;
} ev_actor_publish_port_stats_t;

typedef struct {
    ev_runtime_graph_t *graph;
    ev_actor_id_t source_actor;
    ev_actor_publish_port_stats_t stats;
} ev_actor_publish_port_t;

ev_result_t ev_actor_publish_port_init(ev_actor_publish_port_t *port,
                                       ev_runtime_graph_t *graph,
                                       ev_actor_id_t source_actor);
ev_result_t ev_actor_publish(ev_actor_publish_port_t *port, const ev_msg_t *msg, ev_delivery_report_t *out_report);
ev_result_t ev_actor_send(ev_actor_publish_port_t *port, ev_actor_id_t target_actor, const ev_msg_t *msg);
ev_result_t ev_actor_publish_port_delivery_adapter(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context);
const ev_actor_publish_port_stats_t *ev_actor_publish_port_stats(const ev_actor_publish_port_t *port);

#ifdef __cplusplus
}
#endif

#endif /* EV_ACTOR_PUBLISH_PORT_H */
