#ifndef EV_RUNTIME_GRAPH_H
#define EV_RUNTIME_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "ev/actor_module.h"
#include "ev/actor_runtime.h"
#include "ev/capabilities.h"
#include "ev/fault_bus.h"
#include "ev/ingress_service.h"
#include "ev/lifecycle.h"
#include "ev/metrics_registry.h"
#include "ev/quiescence_service.h"
#include "ev/timer_service.h"
#include "ev/trace_ring.h"

#ifndef EV_RUNTIME_MAILBOX_CAPACITY_MAX
#define EV_RUNTIME_MAILBOX_CAPACITY_MAX 16U
#endif

typedef struct ev_runtime_actor_context {
    struct ev_runtime_graph *graph;
    ev_actor_id_t actor_id;
} ev_runtime_actor_context_t;

typedef struct {
    size_t actor_count;
    size_t pending_actor_messages;
    size_t pending_ingress_events;
    size_t pending_timers;
    uint32_t faults_emitted;
    uint32_t metrics_post_ok;
} ev_runtime_graph_stats_t;

typedef struct ev_runtime_graph {
    ev_actor_registry_t registry;
    ev_actor_runtime_t actor_runtimes[EV_ACTOR_COUNT];
    ev_mailbox_t mailboxes[EV_ACTOR_COUNT];
    ev_msg_t mailbox_storage[EV_ACTOR_COUNT][EV_RUNTIME_MAILBOX_CAPACITY_MAX];
    ev_runtime_actor_context_t actor_contexts[EV_ACTOR_COUNT];
    const ev_actor_module_descriptor_t *descriptors[EV_ACTOR_COUNT];
    ev_actor_lifecycle_state_t lifecycle[EV_ACTOR_COUNT];
    uint8_t actor_enabled[EV_ACTOR_COUNT];

    ev_timer_service_t timer_service;
    ev_ingress_service_t ingress_service;
    ev_quiescence_service_t quiescence_service;
    ev_fault_registry_t faults;
    ev_metric_registry_t metrics;
    ev_trace_ring_t trace_ring;

    ev_board_capability_snapshot_t board_capabilities;
    ev_runtime_capability_snapshot_t runtime_capabilities;
} ev_runtime_graph_t;

typedef struct {
    ev_runtime_graph_t *graph;
    ev_capability_mask_t board_caps;
    ev_capability_mask_t runtime_caps;
    uint8_t requested[EV_ACTOR_COUNT];
    ev_result_t last_error;
} ev_runtime_builder_t;

ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_init(ev_runtime_builder_t *builder, ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_add_module(ev_runtime_builder_t *builder, ev_actor_id_t actor_id);
ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder);
ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder);
ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id);
size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph);
ev_runtime_graph_stats_t ev_runtime_graph_stats(const ev_runtime_graph_t *graph);

#endif
