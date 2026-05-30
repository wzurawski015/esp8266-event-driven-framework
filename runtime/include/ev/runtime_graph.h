#ifndef EV_RUNTIME_GRAPH_H
#define EV_RUNTIME_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "ev/actor_catalog.h"
#include "ev/actor_id.h"
#include "ev/actor_instance.h"
#include "ev/actor_runtime.h"
#include "ev/capabilities.h"
#include "ev/delivery_service.h"
#include "ev/event_id.h"
#include "ev/msg.h"
#include "ev/result.h"
#include "ev/runtime_board_profile.h"
#include "ev/runtime_ports.h"

#ifndef EV_RUNTIME_MAILBOX_CAPACITY_MAX
#define EV_RUNTIME_MAILBOX_CAPACITY_MAX 16U
#endif

/*
 * Public graph objects remain stack/static allocatable on embedded targets,
 * but their implementation fields are deliberately private to runtime/src.
 * The byte count matches the current internal graph implementation; the union
 * member only provides conservative alignment without exposing runtime layout.
 */
#define EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES 23336U

typedef union ev_runtime_graph_opaque_storage {
    unsigned char storage[EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES];
    void *align_ptr;
    uint64_t align_u64;
    long double align_long_double;
} ev_runtime_graph_opaque_storage_t;

typedef struct ev_runtime_graph {
    ev_runtime_graph_opaque_storage_t opaque;
} ev_runtime_graph_t;

typedef struct ev_runtime_actor_context {
    ev_runtime_graph_t *graph;
    ev_actor_id_t actor_id;
} ev_runtime_actor_context_t;

typedef struct {
    ev_runtime_graph_t *graph;
    ev_capability_mask_t board_caps;
    ev_capability_mask_t runtime_caps;
    uint8_t requested[EV_ACTOR_COUNT];
    ev_result_t last_error;
    ev_runtime_ports_t configured_ports;
    ev_runtime_board_profile_t configured_board_profile;
    uint8_t configured_ports_set;
    uint8_t configured_board_profile_set;
    uint32_t route_validation_flags;
} ev_runtime_builder_t;

ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_init(ev_runtime_builder_t *builder, ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_set_ports(ev_runtime_builder_t *builder, const ev_runtime_ports_t *ports);
ev_result_t ev_runtime_builder_set_board_profile(ev_runtime_builder_t *builder, const ev_runtime_board_profile_t *profile);
ev_result_t ev_runtime_builder_add_module(ev_runtime_builder_t *builder, ev_actor_id_t actor_id);
ev_result_t ev_runtime_builder_add_instance(ev_runtime_builder_t *builder, const ev_actor_instance_descriptor_t *instance);
ev_result_t ev_runtime_builder_set_route_validation_flags(ev_runtime_builder_t *builder, uint32_t flags);
ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder);
ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder);

ev_result_t ev_runtime_graph_publish(ev_runtime_graph_t *graph, const ev_msg_t *msg, ev_delivery_report_t *out_report);
ev_result_t ev_runtime_graph_send(ev_runtime_graph_t *graph, ev_actor_id_t target_actor, const ev_msg_t *msg);
ev_result_t ev_runtime_graph_post_event(ev_runtime_graph_t *graph, ev_event_id_t event_id, ev_actor_id_t source_actor, const void *payload, size_t payload_size);
ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id);

ev_result_t ev_runtime_graph_actor_mailbox_offset(const ev_runtime_graph_t *graph,
                                                  ev_actor_id_t actor_id,
                                                  size_t *out_offset);
size_t ev_runtime_graph_actor_mailbox_capacity(const ev_runtime_graph_t *graph, ev_actor_id_t actor_id);
size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_next_deadline_ms(const ev_runtime_graph_t *graph, uint32_t *out_deadline_ms);

#endif
