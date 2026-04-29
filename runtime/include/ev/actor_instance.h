#ifndef EV_ACTOR_INSTANCE_H
#define EV_ACTOR_INSTANCE_H

#include <stddef.h>
#include <stdint.h>

#include "ev/actor_module.h"
#include "ev/runtime_board_profile.h"
#include "ev/runtime_ports.h"

/**
 * @brief Actor-instance initializer with concrete context and injected ports.
 */
typedef ev_result_t (*ev_actor_instance_init_fn_t)(
    void *actor_context,
    const ev_runtime_ports_t *ports,
    const ev_runtime_board_profile_t *profile,
    void *user);

/**
 * @brief Concrete actor instance bound into one runtime graph.
 *
 * Module descriptors describe actor classes. Instance descriptors describe the
 * concrete actor context, handler and port/profile requirements used by one
 * graph build.
 */
typedef struct {
    ev_actor_id_t actor_id;
    const ev_actor_module_descriptor_t *module;
    void *actor_context;
    size_t actor_context_size;
    ev_actor_handler_fn_t handler_fn;
    ev_actor_instance_init_fn_t init_fn;
    ev_actor_quiescence_fn_t quiescence_fn;
    ev_actor_module_stats_fn_t stats_fn;
    ev_actor_module_lifecycle_fn_t lifecycle_fn;
    ev_capability_mask_t required_capabilities;
    ev_capability_mask_t optional_capabilities;
    uint32_t instance_flags;
    void *user;
} ev_actor_instance_descriptor_t;

/**
 * @brief Validate one actor instance descriptor against basic catalog rules.
 */
ev_result_t ev_actor_instance_validate(const ev_actor_instance_descriptor_t *instance);

#endif /* EV_ACTOR_INSTANCE_H */
