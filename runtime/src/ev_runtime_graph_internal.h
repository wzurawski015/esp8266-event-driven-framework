#ifndef EV_RUNTIME_GRAPH_INTERNAL_H
#define EV_RUNTIME_GRAPH_INTERNAL_H

#include "ev/runtime_graph.h"

#include "ev/active_route_table.h"
#include "ev/actor_mailbox_layout_generated.h"
#include "ev/actor_module.h"
#include "ev/actor_runtime.h"
#include "ev/capabilities.h"
#include "ev/compiler.h"
#include "ev/delivery_service.h"
#include "ev/fault_bus.h"
#include "ev/ingress_service.h"
#include "ev/lifecycle.h"
#include "ev/metrics_registry.h"
#include "ev/quiescence_service.h"
#include "ev/runtime_board_profile.h"
#include "ev/runtime_ports.h"
#include "ev/runtime_scheduler.h"
#include "ev/timer_service.h"
#include "ev/trace_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ev_runtime_graph_impl {
    ev_actor_registry_t registry;
    ev_actor_runtime_t actor_runtimes[EV_ACTOR_COUNT];
    ev_mailbox_t mailboxes[EV_ACTOR_COUNT];
    ev_msg_t mailbox_storage[EV_RUNTIME_MAILBOX_TOTAL_CAPACITY];
    ev_runtime_actor_context_t actor_contexts[EV_ACTOR_COUNT];
    const ev_actor_module_descriptor_t *descriptors[EV_ACTOR_COUNT];
    ev_actor_instance_descriptor_t instances[EV_ACTOR_COUNT];
    uint8_t instance_bound[EV_ACTOR_COUNT];
    ev_actor_lifecycle_state_t lifecycle[EV_ACTOR_COUNT];
    uint8_t actor_enabled[EV_ACTOR_COUNT];

    ev_timer_service_t timer_service;
    ev_ingress_service_t ingress_service;
    ev_quiescence_service_t quiescence_service;
    ev_fault_registry_t faults;
    ev_metric_registry_t metrics;
    ev_trace_ring_t trace_ring;
    ev_active_route_table_t active_routes;
    ev_delivery_service_t delivery_service;
    ev_runtime_scheduler_t scheduler;
    uint8_t active_routes_bound;

    ev_board_capability_snapshot_t board_capabilities;
    ev_runtime_capability_snapshot_t runtime_capabilities;
    ev_runtime_ports_t ports;
    ev_runtime_board_profile_t board_profile;
} ev_runtime_graph_impl_t;

EV_STATIC_ASSERT(sizeof(ev_runtime_graph_impl_t) <= EV_RUNTIME_GRAPH_OPAQUE_STORAGE_BYTES,
                 "runtime graph implementation exceeds public opaque storage");

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
EV_STATIC_ASSERT(_Alignof(ev_runtime_graph_impl_t) <= _Alignof(ev_runtime_graph_t),
                 "runtime graph implementation alignment exceeds public opaque storage alignment");
#endif

#define EV_RUNTIME_GRAPH_IMPL(graph_) ((ev_runtime_graph_impl_t *)(void *)&((graph_)->opaque.storage[0]))
#define EV_RUNTIME_GRAPH_IMPL_CONST(graph_) ((const ev_runtime_graph_impl_t *)(const void *)&((graph_)->opaque.storage[0]))

static inline ev_runtime_graph_impl_t *ev_runtime_graph_impl(ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? EV_RUNTIME_GRAPH_IMPL(graph) : NULL;
}

static inline const ev_runtime_graph_impl_t *ev_runtime_graph_impl_const(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? EV_RUNTIME_GRAPH_IMPL_CONST(graph) : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* EV_RUNTIME_GRAPH_INTERNAL_H */
