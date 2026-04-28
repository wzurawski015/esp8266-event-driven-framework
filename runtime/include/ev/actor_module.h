#ifndef EV_ACTOR_MODULE_H
#define EV_ACTOR_MODULE_H

#include <stddef.h>
#include <stdint.h>

#include "ev/actor_runtime.h"
#include "ev/capabilities.h"
#include "ev/fault_bus.h"
#include "ev/lifecycle.h"
#include "ev/quiescence_service.h"
#include "ev/result.h"
#include "ev/route_table.h"

struct ev_runtime_graph;
struct ev_actor_module_descriptor;

typedef ev_result_t (*ev_actor_module_init_fn_t)(struct ev_runtime_graph *graph, const struct ev_actor_module_descriptor *descriptor);
typedef ev_result_t (*ev_actor_module_bind_fn_t)(struct ev_runtime_graph *graph, const struct ev_actor_module_descriptor *descriptor);
typedef ev_result_t (*ev_actor_module_stats_fn_t)(void *actor_context, void *out_stats);
typedef ev_result_t (*ev_actor_module_lifecycle_fn_t)(void *actor_context, ev_actor_lifecycle_state_t old_state, ev_actor_lifecycle_state_t new_state);

typedef struct ev_actor_module_descriptor {
    ev_actor_id_t actor_id;
    const char *module_name;
    ev_capability_mask_t required_board_capabilities;
    ev_capability_mask_t required_runtime_capabilities;
    ev_capability_mask_t provided_capabilities;
    uint32_t hardware_mask;
    ev_execution_domain_t execution_domain;
    size_t mailbox_capacity;
    ev_actor_module_init_fn_t init_fn;
    ev_actor_module_bind_fn_t bind_fn;
    ev_actor_quiescence_fn_t quiescence_fn;
    ev_actor_module_stats_fn_t stats_fn;
    ev_actor_module_lifecycle_fn_t lifecycle_fn;
    ev_fault_id_t fault_policy;
    uint32_t route_policy_flags;
    ev_actor_handler_fn_t handler_fn;
} ev_actor_module_descriptor_t;

const ev_actor_module_descriptor_t *ev_actor_module_table(size_t *out_count);
const ev_actor_module_descriptor_t *ev_actor_module_find(ev_actor_id_t actor_id);

ev_result_t ev_default_module_init(struct ev_runtime_graph *graph, const ev_actor_module_descriptor_t *descriptor);
ev_result_t ev_default_module_bind(struct ev_runtime_graph *graph, const ev_actor_module_descriptor_t *descriptor);
ev_result_t ev_default_quiescence(void *actor_context, ev_quiescence_report_t *report);
ev_result_t ev_default_module_stats(void *actor_context, void *out_stats);
ev_result_t ev_default_lifecycle(void *actor_context, ev_actor_lifecycle_state_t old_state, ev_actor_lifecycle_state_t new_state);

ev_result_t ev_framework_actor_handle(void *actor_context, const ev_msg_t *msg);
ev_result_t ev_fault_actor_handle(void *actor_context, const ev_msg_t *msg);
ev_result_t ev_metrics_actor_handle(void *actor_context, const ev_msg_t *msg);

#endif
