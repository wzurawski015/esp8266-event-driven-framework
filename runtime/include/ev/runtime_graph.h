#ifndef EV_RUNTIME_GRAPH_H
#define EV_RUNTIME_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "ev/active_route_table.h"
#include "ev/delivery_service.h"
#include "ev/actor_instance.h"
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
#include "ev/runtime_board_profile.h"
#include "ev/runtime_ports.h"
#include "ev/runtime_scheduler.h"

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
} ev_runtime_graph_t;

typedef struct {
    ev_runtime_graph_t *graph;
    ev_capability_mask_t board_caps;
    ev_capability_mask_t runtime_caps;
    uint8_t requested[EV_ACTOR_COUNT];
    ev_result_t last_error;
    ev_runtime_ports_t ports;
    ev_runtime_board_profile_t board_profile;
    uint8_t ports_set;
    uint8_t board_profile_set;
    uint32_t route_validation_flags;
} ev_runtime_builder_t;

ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_init(ev_runtime_builder_t *builder, ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps);
ev_result_t ev_runtime_builder_set_ports(ev_runtime_builder_t *builder, const ev_runtime_ports_t *ports);
ev_result_t ev_runtime_builder_set_board_profile(ev_runtime_builder_t *builder, const ev_runtime_board_profile_t *profile);
ev_result_t ev_runtime_builder_add_module(ev_runtime_builder_t *builder, ev_actor_id_t actor_id);
ev_result_t ev_runtime_builder_add_instance(ev_runtime_builder_t *builder, const ev_actor_instance_descriptor_t *instance);
ev_result_t ev_runtime_builder_set_route_validation_flags(ev_runtime_builder_t *builder, uint32_t flags);
const ev_active_route_table_t *ev_runtime_graph_active_routes(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_publish(ev_runtime_graph_t *graph, const ev_msg_t *msg, ev_delivery_report_t *out_report);
ev_result_t ev_runtime_graph_send(ev_runtime_graph_t *graph, ev_actor_id_t target_actor, const ev_msg_t *msg);
ev_result_t ev_runtime_graph_post_event(ev_runtime_graph_t *graph, ev_event_id_t event_id, ev_actor_id_t source_actor, const void *payload, size_t payload_size);
ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder);
ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder);
ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id);
ev_result_t ev_runtime_graph_schedule_periodic(ev_runtime_graph_t *graph,
                                               uint32_t now_ms,
                                               uint32_t period_ms,
                                               ev_actor_id_t target_actor,
                                               ev_event_id_t event_id,
                                               uint32_t arg0,
                                               ev_timer_token_t *out_token);
size_t ev_runtime_graph_scheduler_pending(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_poll_scheduler_once(ev_runtime_graph_t *graph, size_t turn_budget, ev_system_pump_report_t *out_report);
size_t ev_runtime_graph_publish_due_timers(ev_runtime_graph_t *graph,
                                           uint32_t now_ms,
                                           ev_timer_publish_fn_t deliver,
                                           void *deliver_ctx,
                                           size_t max_publish);
const ev_system_pump_stats_t *ev_runtime_graph_system_pump_stats(const ev_runtime_graph_t *graph);
const ev_domain_pump_stats_t *ev_runtime_graph_domain_pump_stats(const ev_runtime_graph_t *graph, ev_execution_domain_t domain);
size_t ev_runtime_graph_domain_pending(const ev_runtime_graph_t *graph, ev_execution_domain_t domain);
uint32_t ev_runtime_graph_timer_published_count(const ev_runtime_graph_t *graph);
size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_next_deadline_ms(const ev_runtime_graph_t *graph, uint32_t *out_deadline_ms);
ev_runtime_graph_stats_t ev_runtime_graph_stats(const ev_runtime_graph_t *graph);

#endif
