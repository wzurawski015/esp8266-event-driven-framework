#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"

#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/compiler.h"
#include "ev/event_catalog.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_ports.h"
#include "ev/runtime_board_profile.h"
#include "ev_runtime_graph_internal.h"

EV_STATIC_ASSERT(EV_ACTOR_MAILBOX_LAYOUT_GENERATED_COUNT == EV_ACTOR_COUNT,
                 "actor mailbox layout count mismatch");
EV_STATIC_ASSERT(EV_RUNTIME_MAILBOX_TOTAL_CAPACITY > 0U,
                 "runtime mailbox storage must not be empty");
EV_STATIC_ASSERT(EV_RUNTIME_MAILBOX_TOTAL_CAPACITY <= ((size_t)EV_ACTOR_COUNT * EV_RUNTIME_MAILBOX_CAPACITY_MAX),
                 "runtime mailbox layout exceeds maximum envelope");
EV_STATIC_ASSERT(sizeof(((ev_runtime_graph_impl_t *)0)->mailbox_storage) ==
                     (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY * sizeof(ev_msg_t)),
                 "runtime mailbox storage size mismatch");

static int ev_runtime_mailbox_capacity_is_power_of_two(size_t capacity)
{
    return (capacity != 0U) && ((capacity & (capacity - 1U)) == 0U);
}

ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps)
{
    size_t i;

    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(graph, 0, sizeof(*graph));
    (void)ev_actor_registry_init(&EV_RUNTIME_GRAPH_IMPL(graph)->registry);
    ev_timer_service_init(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service);
    ev_ingress_service_init(&EV_RUNTIME_GRAPH_IMPL(graph)->ingress_service);
    ev_quiescence_service_init(&EV_RUNTIME_GRAPH_IMPL(graph)->quiescence_service);
    ev_fault_registry_init(&EV_RUNTIME_GRAPH_IMPL(graph)->faults);
    ev_metric_registry_init(&EV_RUNTIME_GRAPH_IMPL(graph)->metrics);
    ev_trace_ring_init(&EV_RUNTIME_GRAPH_IMPL(graph)->trace_ring);
    ev_active_route_table_init(&EV_RUNTIME_GRAPH_IMPL(graph)->active_routes);
    ev_delivery_service_init(&EV_RUNTIME_GRAPH_IMPL(graph)->delivery_service, graph);

    EV_RUNTIME_GRAPH_IMPL(graph)->board_capabilities.configured = board_caps;
    EV_RUNTIME_GRAPH_IMPL(graph)->board_capabilities.active = board_caps;
    EV_RUNTIME_GRAPH_IMPL(graph)->board_capabilities.observed = 0U;
    EV_RUNTIME_GRAPH_IMPL(graph)->runtime_capabilities.configured = runtime_caps;
    EV_RUNTIME_GRAPH_IMPL(graph)->runtime_capabilities.active = runtime_caps;
    EV_RUNTIME_GRAPH_IMPL(graph)->runtime_capabilities.required = 0U;

    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        EV_RUNTIME_GRAPH_IMPL(graph)->actor_contexts[i].graph = graph;
        EV_RUNTIME_GRAPH_IMPL(graph)->actor_contexts[i].actor_id = (ev_actor_id_t)i;
        EV_RUNTIME_GRAPH_IMPL(graph)->lifecycle[i] = EV_ACTOR_STATE_UNINITIALIZED;
    }
    return EV_OK;
}

ev_result_t ev_runtime_builder_init(ev_runtime_builder_t *builder, ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps)
{
    if ((builder == NULL) || (graph == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    (void)memset(builder, 0, sizeof(*builder));
    builder->graph = graph;
    builder->board_caps = board_caps;
    builder->runtime_caps = runtime_caps;
    builder->last_error = EV_OK;
    return ev_runtime_graph_init(graph, board_caps, runtime_caps);
}


ev_result_t ev_runtime_builder_set_ports(ev_runtime_builder_t *builder, const ev_runtime_ports_t *ports)
{
    if ((builder == NULL) || (builder->graph == NULL) || (ports == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    builder->configured_ports = *ports;
    builder->configured_ports_set = 1U;
    EV_RUNTIME_GRAPH_IMPL(builder->graph)->ports = *ports;
    return EV_OK;
}

ev_result_t ev_runtime_builder_set_board_profile(ev_runtime_builder_t *builder, const ev_runtime_board_profile_t *profile)
{
    if ((builder == NULL) || (builder->graph == NULL) || (profile == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    builder->configured_board_profile = *profile;
    builder->configured_board_profile_set = 1U;
    EV_RUNTIME_GRAPH_IMPL(builder->graph)->board_profile = *profile;
    if (profile->configured_capabilities != 0U) {
        builder->board_caps = profile->configured_capabilities;
        EV_RUNTIME_GRAPH_IMPL(builder->graph)->board_capabilities.configured = profile->configured_capabilities;
    }
    if (profile->active_capabilities != 0U) {
        EV_RUNTIME_GRAPH_IMPL(builder->graph)->board_capabilities.active = profile->active_capabilities;
    }
    EV_RUNTIME_GRAPH_IMPL(builder->graph)->board_capabilities.observed = profile->observed_capabilities;
    return EV_OK;
}

ev_result_t ev_runtime_builder_add_instance(ev_runtime_builder_t *builder, const ev_actor_instance_descriptor_t *instance)
{
    ev_result_t rc;
    ev_actor_instance_descriptor_t stored;

    if ((builder == NULL) || (builder->graph == NULL) || (instance == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_actor_instance_validate(instance);
    if (rc != EV_OK) {
        builder->last_error = rc;
        return rc;
    }
    if ((instance->required_capabilities & builder->board_caps) != instance->required_capabilities) {
        builder->last_error = EV_ERR_NO_CAPABILITY;
        return builder->last_error;
    }

    stored = *instance;
    if (stored.handler_fn == NULL) {
        stored.handler_fn = stored.module->handler_fn;
    }
    if (stored.quiescence_fn == NULL) {
        stored.quiescence_fn = stored.module->quiescence_fn;
    }
    if (stored.stats_fn == NULL) {
        stored.stats_fn = stored.module->stats_fn;
    }
    if (stored.lifecycle_fn == NULL) {
        stored.lifecycle_fn = stored.module->lifecycle_fn;
    }
    EV_RUNTIME_GRAPH_IMPL(builder->graph)->instances[stored.actor_id] = stored;
    EV_RUNTIME_GRAPH_IMPL(builder->graph)->instance_bound[stored.actor_id] = 1U;
    builder->requested[stored.actor_id] = 1U;
    return EV_OK;
}

ev_result_t ev_runtime_builder_add_module(ev_runtime_builder_t *builder, ev_actor_id_t actor_id)
{
    const ev_actor_module_descriptor_t *descriptor;

    if (builder == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(actor_id)) {
        builder->last_error = EV_ERR_OUT_OF_RANGE;
        return builder->last_error;
    }
    descriptor = ev_actor_module_find(actor_id);
    if (descriptor == NULL) {
        builder->last_error = EV_ERR_NOT_FOUND;
        return builder->last_error;
    }
    if ((descriptor->required_board_capabilities & builder->board_caps) != descriptor->required_board_capabilities) {
        builder->last_error = EV_ERR_NO_CAPABILITY;
        return builder->last_error;
    }
    if ((descriptor->required_runtime_capabilities & builder->runtime_caps) != descriptor->required_runtime_capabilities) {
        builder->last_error = EV_ERR_NO_CAPABILITY;
        return builder->last_error;
    }

    {
        ev_actor_instance_descriptor_t instance;
        (void)memset(&instance, 0, sizeof(instance));
        instance.actor_id = actor_id;
        instance.module = descriptor;
        instance.actor_context = &EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_contexts[actor_id];
        instance.actor_context_size = sizeof(EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_contexts[actor_id]);
        instance.handler_fn = descriptor->handler_fn;
        instance.quiescence_fn = descriptor->quiescence_fn;
        instance.stats_fn = descriptor->stats_fn;
        instance.lifecycle_fn = descriptor->lifecycle_fn;
        instance.required_capabilities = descriptor->required_board_capabilities;
        return ev_runtime_builder_add_instance(builder, &instance);
    }
}


ev_result_t ev_runtime_builder_set_route_validation_flags(ev_runtime_builder_t *builder, uint32_t flags)
{
    if (builder == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    builder->route_validation_flags = flags;
    return EV_OK;
}

static int ev_runtime_builder_route_qos_supported(const ev_actor_module_descriptor_t *descriptor, ev_route_qos_t qos)
{
    if (descriptor == NULL) {
        return 0;
    }
    if (ev_route_qos_is_valid(qos) == 0) {
        return 0;
    }
    if ((descriptor->route_policy_flags != 0U) && (qos != (ev_route_qos_t)descriptor->route_policy_flags)) {
        if ((descriptor->route_policy_flags == EV_ROUTE_QOS_WAKEUP_CRITICAL) && (qos == EV_ROUTE_QOS_CRITICAL)) {
            return 1;
        }
        if ((descriptor->route_policy_flags == EV_ROUTE_QOS_TELEMETRY) &&
            ((qos == EV_ROUTE_QOS_TELEMETRY) || (qos == EV_ROUTE_QOS_BEST_EFFORT) || (qos == EV_ROUTE_QOS_LOSSY) || (qos == EV_ROUTE_QOS_CRITICAL))) {
            return 1;
        }
        if ((descriptor->route_policy_flags == EV_ROUTE_QOS_COMMAND) && ((qos == EV_ROUTE_QOS_COMMAND) || (qos == EV_ROUTE_QOS_CRITICAL))) {
            return 1;
        }
        return 0;
    }
    return 1;
}

static ev_active_route_state_t ev_runtime_builder_classify_route(ev_runtime_builder_t *builder, const ev_route_t *route, ev_result_t *out_reason)
{
    const ev_actor_module_descriptor_t *descriptor;
    ev_capability_mask_t missing_board;
    ev_capability_mask_t missing_runtime;

    if ((builder == NULL) || (route == NULL)) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_INVALID_ARG;
        }
        return EV_ACTIVE_ROUTE_REJECTED_INVALID_EVENT;
    }
    if (!ev_event_id_is_valid(route->event_id)) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_OUT_OF_RANGE;
        }
        return EV_ACTIVE_ROUTE_REJECTED_INVALID_EVENT;
    }
    if (!ev_actor_id_is_valid(route->target_actor)) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_OUT_OF_RANGE;
        }
        return EV_ACTIVE_ROUTE_REJECTED_INVALID_ACTOR;
    }
    descriptor = ev_actor_module_find(route->target_actor);
    if (descriptor == NULL) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_NOT_FOUND;
        }
        return EV_ACTIVE_ROUTE_REJECTED_INVALID_ACTOR;
    }
    if (ev_runtime_builder_route_qos_supported(descriptor, route->qos) == 0) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_POLICY;
        }
        return EV_ACTIVE_ROUTE_REJECTED_QOS_CONFLICT;
    }
    if (builder->requested[route->target_actor] != 0U) {
        if (out_reason != NULL) {
            *out_reason = EV_OK;
        }
        return EV_ACTIVE_ROUTE_ENABLED;
    }

    missing_board = descriptor->required_board_capabilities & ~builder->board_caps;
    missing_runtime = descriptor->required_runtime_capabilities & ~builder->runtime_caps;
    if ((missing_board != 0U) || (missing_runtime != 0U) || ((builder->route_validation_flags & EV_RUNTIME_ROUTE_VALIDATE_STRICT_MANDATORY) == 0U)) {
        if (out_reason != NULL) {
            *out_reason = EV_ERR_NO_CAPABILITY;
        }
        return EV_ACTIVE_ROUTE_OPTIONAL_DISABLED;
    }

    if (out_reason != NULL) {
        *out_reason = EV_ERR_NOT_FOUND;
    }
    return EV_ACTIVE_ROUTE_REJECTED_MISSING_MANDATORY_ACTOR;
}

static void ev_runtime_builder_emit_route_fault(ev_runtime_graph_t *graph,
                                                const ev_route_t *route,
                                                ev_active_route_state_t state,
                                                ev_result_t reason)
{
    ev_fault_payload_t fault;
    if ((graph == NULL) || (route == NULL)) {
        return;
    }
    memset(&fault, 0, sizeof(fault));
    fault.source_actor = ACT_RUNTIME;
    fault.triggering_event = route->event_id;
    fault.arg0 = (uint32_t)route->target_actor;
    fault.arg1 = (uint32_t)(-reason);
    fault.severity = EV_FAULT_SEV_ERROR;
    switch (state) {
    case EV_ACTIVE_ROUTE_REJECTED_QOS_CONFLICT:
        fault.fault_id = EV_FAULT_ROUTE_QOS_CONFLICT;
        break;
    case EV_ACTIVE_ROUTE_REJECTED_MISSING_MANDATORY_ACTOR:
        fault.fault_id = EV_FAULT_ROUTE_MISSING_CRITICAL_TARGET;
        fault.severity = EV_FAULT_SEV_CRITICAL;
        break;
    case EV_ACTIVE_ROUTE_REJECTED_OVERFLOW:
        fault.fault_id = EV_FAULT_ACTIVE_ROUTE_TABLE_OVERFLOW;
        break;
    default:
        fault.fault_id = EV_FAULT_ROUTE_VALIDATION_REJECTED;
        break;
    }
    if (ev_fault_emit(&EV_RUNTIME_GRAPH_IMPL(graph)->faults, &fault) == EV_OK) {
        (void)ev_metric_increment(&EV_RUNTIME_GRAPH_IMPL(graph)->metrics, EV_METRIC_FAULT_EMITTED, 1U);
    }
}

ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder)
{
    size_t i;
    ev_result_t rc;

    if ((builder == NULL) || (builder->graph == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes_bound = 0U;
    ev_active_route_table_init(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes);
    for (i = 0U; i < ev_route_count(); ++i) {
        const ev_route_t *route = ev_route_at(i);
        ev_active_route_state_t state;
        ev_result_t reason = EV_OK;
        if (route == NULL) {
            continue;
        }
        state = ev_runtime_builder_classify_route(builder, route, &reason);
        rc = ev_active_route_table_add(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes, route, state, reason);
        if (rc != EV_OK) {
            ev_route_t overflow_route = *route;
            (void)ev_active_route_table_add(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes, &overflow_route, EV_ACTIVE_ROUTE_REJECTED_OVERFLOW, EV_ERR_FULL);
            (void)ev_metric_increment(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->metrics, EV_METRIC_ROUTE_ACTIVE_TABLE_OVERFLOW, 1U);
            ev_runtime_builder_emit_route_fault(builder->graph, route, EV_ACTIVE_ROUTE_REJECTED_OVERFLOW, EV_ERR_FULL);
            builder->last_error = EV_ERR_FULL;
            return builder->last_error;
        }
        if (state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
            (void)ev_metric_increment(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->metrics, EV_METRIC_ROUTE_OPTIONAL_DISABLED, 1U);
        } else if (state != EV_ACTIVE_ROUTE_ENABLED) {
            (void)ev_metric_increment(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->metrics, EV_METRIC_ROUTE_VALIDATION_REJECTED, 1U);
            if (state == EV_ACTIVE_ROUTE_REJECTED_QOS_CONFLICT) {
                (void)ev_metric_increment(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->metrics, EV_METRIC_ROUTE_QOS_CONFLICT, 1U);
            }
            ev_runtime_builder_emit_route_fault(builder->graph, route, state, reason);
            if ((builder->route_validation_flags & EV_RUNTIME_ROUTE_VALIDATE_STRICT_MANDATORY) != 0U) {
                builder->last_error = reason;
                return reason;
            }
        }
    }

    rc = ev_active_route_table_finalize_spans(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes);
    if (rc != EV_OK) {
        builder->last_error = rc;
        return rc;
    }

    EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes_bound = 1U;
    return EV_OK;
}


static uint32_t ev_runtime_builder_active_domain_mask(const ev_runtime_builder_t *builder)
{
    uint32_t mask = 0U;
    size_t i;

    if (builder == NULL) {
        return 0U;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (builder->requested[i] != 0U) {
            const ev_actor_meta_t *meta = ev_actor_meta((ev_actor_id_t)i);
            if (meta != NULL) {
                mask |= EV_RUNTIME_DOMAIN_MASK(meta->execution_domain);
            }
        }
    }
    return mask;
}

static ev_result_t ev_runtime_mailbox_storage_for_actor(ev_runtime_graph_t *graph,
                                                        ev_actor_id_t actor_id,
                                                        const ev_actor_meta_t *meta,
                                                        ev_msg_t **out_storage,
                                                        size_t *out_capacity)
{
    ev_actor_mailbox_layout_entry_t layout;
    size_t expected_capacity;

    if ((graph == NULL) || (meta == NULL) || (out_storage == NULL) || (out_capacity == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_actor_mailbox_layout_lookup(actor_id, &layout) == 0) {
        return EV_ERR_CONTRACT;
    }

    expected_capacity = ev_mailbox_kind_capacity(meta->mailbox_kind);
    if ((layout.actor_id != actor_id) ||
        (layout.mailbox_kind != meta->mailbox_kind) ||
        (layout.capacity != expected_capacity) ||
        (expected_capacity == 0U) ||
        (layout.capacity > EV_RUNTIME_MAILBOX_CAPACITY_MAX) ||
        (layout.offset > EV_RUNTIME_MAILBOX_TOTAL_CAPACITY) ||
        (layout.capacity > (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY - layout.offset)) ||
        (ev_runtime_mailbox_capacity_is_power_of_two(layout.capacity) == 0)) {
        return EV_ERR_CONTRACT;
    }

    *out_storage = &EV_RUNTIME_GRAPH_IMPL(graph)->mailbox_storage[layout.offset];
    *out_capacity = layout.capacity;
    return EV_OK;
}

ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder)
{
    size_t i;

    if ((builder == NULL) || (builder->graph == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (EV_RUNTIME_GRAPH_IMPL(builder->graph)->active_routes_bound == 0U) {
        ev_result_t bind_rc = ev_runtime_builder_bind_routes(builder);
        if (bind_rc != EV_OK) {
            return bind_rc;
        }
    }

    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (builder->requested[i] != 0U) {
            ev_actor_id_t actor_id = (ev_actor_id_t)i;
            const ev_actor_meta_t *meta = ev_actor_meta(actor_id);
            const ev_actor_module_descriptor_t *descriptor = ev_actor_module_find(actor_id);
            size_t cap;
            ev_msg_t *mailbox_storage;
            ev_result_t rc;

            if ((meta == NULL) || (descriptor == NULL)) {
                builder->last_error = EV_ERR_NOT_FOUND;
                return builder->last_error;
            }
            rc = ev_runtime_mailbox_storage_for_actor(builder->graph, actor_id, meta, &mailbox_storage, &cap);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }

            void *actor_context = &EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_contexts[i];
            ev_actor_handler_fn_t handler_fn = descriptor->handler_fn;
            if (EV_RUNTIME_GRAPH_IMPL(builder->graph)->instance_bound[i] != 0U) {
                const ev_actor_instance_descriptor_t *instance = &EV_RUNTIME_GRAPH_IMPL(builder->graph)->instances[i];
                if (instance->actor_context != NULL) {
                    actor_context = instance->actor_context;
                }
                if (instance->handler_fn != NULL) {
                    handler_fn = instance->handler_fn;
                }
            }
            rc = ev_mailbox_init(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->mailboxes[i], meta->mailbox_kind, mailbox_storage, cap);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            rc = ev_actor_runtime_init(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_runtimes[i], actor_id, &EV_RUNTIME_GRAPH_IMPL(builder->graph)->mailboxes[i], handler_fn, actor_context);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            rc = ev_actor_registry_bind(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->registry, &EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_runtimes[i]);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_enabled[i] = 1U;
            EV_RUNTIME_GRAPH_IMPL(builder->graph)->descriptors[i] = descriptor;
            EV_RUNTIME_GRAPH_IMPL(builder->graph)->lifecycle[i] = EV_ACTOR_STATE_READY;
            if ((EV_RUNTIME_GRAPH_IMPL(builder->graph)->instance_bound[i] != 0U) && (EV_RUNTIME_GRAPH_IMPL(builder->graph)->instances[i].init_fn != NULL)) {
                rc = EV_RUNTIME_GRAPH_IMPL(builder->graph)->instances[i].init_fn(EV_RUNTIME_GRAPH_IMPL(builder->graph)->actor_runtimes[i].actor_context,
                                                         &EV_RUNTIME_GRAPH_IMPL(builder->graph)->ports,
                                                         &EV_RUNTIME_GRAPH_IMPL(builder->graph)->board_profile,
                                                         EV_RUNTIME_GRAPH_IMPL(builder->graph)->instances[i].user);
                if (rc != EV_OK) {
                    builder->last_error = rc;
                    return rc;
                }
            }
            if (descriptor->init_fn != NULL) {
                rc = descriptor->init_fn(builder->graph, descriptor);
                if (rc != EV_OK) {
                    builder->last_error = rc;
                    return rc;
                }
            }
            if (descriptor->bind_fn != NULL) {
                rc = descriptor->bind_fn(builder->graph, descriptor);
                if (rc != EV_OK) {
                    builder->last_error = rc;
                    return rc;
                }
            }
        }
    }

    {
        ev_result_t sched_rc = ev_runtime_scheduler_init(&EV_RUNTIME_GRAPH_IMPL(builder->graph)->scheduler,
                                                         &EV_RUNTIME_GRAPH_IMPL(builder->graph)->registry,
                                                         ev_runtime_builder_active_domain_mask(builder));
        if (sched_rc != EV_OK) {
            builder->last_error = sched_rc;
            return sched_rc;
        }
    }

    return EV_OK;
}


ev_result_t ev_runtime_graph_publish(ev_runtime_graph_t *graph, const ev_msg_t *msg, ev_delivery_report_t *out_report)
{
    if ((graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_delivery_publish(&EV_RUNTIME_GRAPH_IMPL(graph)->delivery_service, msg, out_report);
}

ev_result_t ev_runtime_graph_send(ev_runtime_graph_t *graph, ev_actor_id_t target_actor, const ev_msg_t *msg)
{
    ev_msg_t send_msg;

    if ((graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(target_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    send_msg = *msg;
    send_msg.target_actor = target_actor;
    return ev_actor_registry_delivery(target_actor, &send_msg, &EV_RUNTIME_GRAPH_IMPL(graph)->registry);
}

ev_result_t ev_runtime_graph_post_event(ev_runtime_graph_t *graph, ev_event_id_t event_id, ev_actor_id_t source_actor, const void *payload, size_t payload_size)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_result_t rc;
    ev_delivery_report_t report;

    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_msg_init_publish(&msg, event_id, source_actor);
    if (rc != EV_OK) {
        return rc;
    }
    if (payload_size > 0U) {
        rc = ev_msg_set_inline_payload(&msg, payload, payload_size);
        if (rc != EV_OK) {
            return rc;
        }
    }
    return ev_runtime_graph_publish(graph, &msg, &report);
}

const ev_active_route_table_t *ev_runtime_graph_route_table(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? &EV_RUNTIME_GRAPH_IMPL(graph)->active_routes : NULL;
}

size_t ev_runtime_graph_configured_mailbox_slots(void)
{
    return EV_RUNTIME_MAILBOX_TOTAL_CAPACITY;
}

size_t ev_runtime_graph_configured_mailbox_bytes(void)
{
    return EV_RUNTIME_MAILBOX_TOTAL_CAPACITY * sizeof(ev_msg_t);
}

ev_result_t ev_runtime_graph_actor_mailbox_offset(const ev_runtime_graph_t *graph,
                                                                 ev_actor_id_t actor_id,
                                                                 size_t *out_offset)
{
    ev_actor_mailbox_layout_entry_t layout;

    if ((graph == NULL) || (out_offset == NULL) || !ev_actor_id_is_valid(actor_id)) {
        return EV_ERR_INVALID_ARG;
    }
    if (EV_RUNTIME_GRAPH_IMPL(graph)->actor_enabled[actor_id] == 0U) {
        return EV_ERR_STATE;
    }
    if (ev_actor_mailbox_layout_lookup(actor_id, &layout) == 0) {
        return EV_ERR_CONTRACT;
    }
    if ((layout.offset >= EV_RUNTIME_MAILBOX_TOTAL_CAPACITY) ||
        (layout.capacity > (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY - layout.offset)) ||
        (EV_RUNTIME_GRAPH_IMPL(graph)->mailboxes[actor_id].storage != &EV_RUNTIME_GRAPH_IMPL(graph)->mailbox_storage[layout.offset]) ||
        (EV_RUNTIME_GRAPH_IMPL(graph)->mailboxes[actor_id].storage_count != layout.capacity)) {
        return EV_ERR_CONTRACT;
    }

    *out_offset = layout.offset;
    return EV_OK;
}

size_t ev_runtime_graph_actor_mailbox_capacity(const ev_runtime_graph_t *graph, ev_actor_id_t actor_id)
{
    if ((graph == NULL) || !ev_actor_id_is_valid(actor_id) || (EV_RUNTIME_GRAPH_IMPL(graph)->actor_enabled[actor_id] == 0U)) {
        return 0U;
    }
    return EV_RUNTIME_GRAPH_IMPL(graph)->mailboxes[actor_id].storage_count;
}

uint32_t ev_runtime_graph_metric_value(const ev_runtime_graph_t *graph, ev_metric_id_t metric_id)
{
    ev_metric_sample_t sample;

    if ((graph == NULL) || (ev_metric_read(&EV_RUNTIME_GRAPH_IMPL(graph)->metrics, metric_id, &sample) != EV_OK)) {
        return 0U;
    }
    return sample.value;
}

size_t ev_runtime_graph_system_pump_bound_count(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? ev_system_pump_bound_count(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler.system) : 0U;
}

uint32_t ev_runtime_graph_scheduler_poll_count(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? EV_RUNTIME_GRAPH_IMPL(graph)->scheduler.poll_calls : 0U;
}

ev_result_t ev_runtime_graph_schedule_oneshot(ev_runtime_graph_t *graph,
                                              uint32_t now_ms,
                                              uint32_t delay_ms,
                                              ev_actor_id_t target_actor,
                                              ev_event_id_t event_id,
                                              uint32_t arg0,
                                              ev_timer_token_t *out_token)
{
    if ((graph == NULL) || (out_token == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_schedule_oneshot(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service,
                                     now_ms,
                                     delay_ms,
                                     target_actor,
                                     event_id,
                                     arg0,
                                     out_token);
}

size_t ev_runtime_graph_timer_pending_count(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? ev_timer_pending_count(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service) : 0U;
}

ev_result_t ev_runtime_graph_trace_record(ev_runtime_graph_t *graph, const ev_trace_record_t *record)
{
    if ((graph == NULL) || (record == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_trace_record(&EV_RUNTIME_GRAPH_IMPL(graph)->trace_ring, record);
}

void ev_runtime_graph_trace_clear(ev_runtime_graph_t *graph)
{
    if (graph != NULL) {
        ev_trace_clear(&EV_RUNTIME_GRAPH_IMPL(graph)->trace_ring);
    }
}

size_t ev_runtime_graph_trace_drain(ev_runtime_graph_t *graph, ev_trace_record_t *out_records, size_t max_records)
{
    if ((graph == NULL) || ((out_records == NULL) && (max_records > 0U))) {
        return 0U;
    }
    return ev_trace_drain(&EV_RUNTIME_GRAPH_IMPL(graph)->trace_ring, out_records, max_records);
}

ev_result_t ev_runtime_graph_schedule_periodic(ev_runtime_graph_t *graph,
                                               uint32_t now_ms,
                                               uint32_t period_ms,
                                               ev_actor_id_t target_actor,
                                               ev_event_id_t event_id,
                                               uint32_t arg0,
                                               ev_timer_token_t *out_token)
{
    if ((graph == NULL) || (out_token == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_schedule_periodic(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service,
                                      now_ms,
                                      period_ms,
                                      target_actor,
                                      event_id,
                                      arg0,
                                      out_token);
}

size_t ev_runtime_graph_scheduler_pending(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? ev_runtime_scheduler_pending(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler) : 0U;
}

ev_result_t ev_runtime_graph_poll_scheduler_once(ev_runtime_graph_t *graph, size_t turn_budget, ev_system_pump_report_t *out_report)
{
    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_runtime_scheduler_poll_once(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler, turn_budget, out_report);
}

size_t ev_runtime_graph_publish_due_timers(ev_runtime_graph_t *graph,
                                           uint32_t now_ms,
                                           ev_timer_publish_fn_t deliver,
                                           void *deliver_ctx,
                                           size_t max_publish)
{
    if (graph == NULL) {
        return 0U;
    }
    return ev_timer_publish_due(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service, now_ms, deliver, deliver_ctx, max_publish);
}

const ev_system_pump_stats_t *ev_runtime_graph_system_pump_stats(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? ev_system_pump_stats(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler.system) : NULL;
}

const ev_domain_pump_stats_t *ev_runtime_graph_domain_pump_stats(const ev_runtime_graph_t *graph, ev_execution_domain_t domain)
{
    if ((graph == NULL) || ((uint32_t)domain >= (uint32_t)EV_DOMAIN_COUNT)) {
        return NULL;
    }
    return ev_domain_pump_stats(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler.domains[domain]);
}

size_t ev_runtime_graph_domain_pending(const ev_runtime_graph_t *graph, ev_execution_domain_t domain)
{
    if ((graph == NULL) || ((uint32_t)domain >= (uint32_t)EV_DOMAIN_COUNT)) {
        return 0U;
    }
    return ev_domain_pump_pending(&EV_RUNTIME_GRAPH_IMPL(graph)->scheduler.domains[domain]);
}

uint32_t ev_runtime_graph_timer_published_count(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? EV_RUNTIME_GRAPH_IMPL(graph)->timer_service.published : 0U;
}

ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id)
{
    if ((graph == NULL) || !ev_actor_id_is_valid(actor_id) || (EV_RUNTIME_GRAPH_IMPL(graph)->actor_enabled[actor_id] == 0U)) {
        return NULL;
    }
    return &EV_RUNTIME_GRAPH_IMPL(graph)->actor_runtimes[actor_id];
}

ev_result_t ev_runtime_graph_next_deadline_ms(const ev_runtime_graph_t *graph, uint32_t *out_deadline_ms)
{
    if ((graph == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_next_deadline_ms(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service, out_deadline_ms);
}

size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph)
{
    size_t i;
    size_t pending = 0U;

    if (graph == NULL) {
        return 0U;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (EV_RUNTIME_GRAPH_IMPL(graph)->actor_enabled[i] != 0U) {
            pending += ev_mailbox_count(&EV_RUNTIME_GRAPH_IMPL(graph)->mailboxes[i]);
        }
    }
    pending += ev_ingress_pending(&EV_RUNTIME_GRAPH_IMPL(graph)->ingress_service);
    pending += ev_timer_pending_count(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service);
    return pending;
}

ev_runtime_graph_stats_t ev_runtime_graph_stats(const ev_runtime_graph_t *graph)
{
    size_t i;
    ev_runtime_graph_stats_t stats;

    (void)memset(&stats, 0, sizeof(stats));
    if (graph == NULL) {
        return stats;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (EV_RUNTIME_GRAPH_IMPL(graph)->actor_enabled[i] != 0U) {
            stats.actor_count++;
            stats.pending_actor_messages += ev_mailbox_count(&EV_RUNTIME_GRAPH_IMPL(graph)->mailboxes[i]);
        }
    }
    stats.pending_ingress_events = ev_ingress_pending(&EV_RUNTIME_GRAPH_IMPL(graph)->ingress_service);
    stats.pending_timers = ev_timer_pending_count(&EV_RUNTIME_GRAPH_IMPL(graph)->timer_service);
    stats.faults_emitted = EV_RUNTIME_GRAPH_IMPL(graph)->faults.emitted;
    stats.metrics_post_ok = EV_RUNTIME_GRAPH_IMPL(graph)->metrics.values[EV_METRIC_POST_OK];
    return stats;
}
