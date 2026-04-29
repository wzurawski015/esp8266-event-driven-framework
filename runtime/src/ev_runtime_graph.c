#include "ev/runtime_graph.h"

#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/event_catalog.h"
#include "ev/metrics_registry.h"
#include "ev/runtime_ports.h"
#include "ev/runtime_board_profile.h"

ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, ev_capability_mask_t board_caps, ev_capability_mask_t runtime_caps)
{
    size_t i;

    if (graph == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    (void)memset(graph, 0, sizeof(*graph));
    (void)ev_actor_registry_init(&graph->registry);
    ev_timer_service_init(&graph->timer_service);
    ev_ingress_service_init(&graph->ingress_service);
    ev_quiescence_service_init(&graph->quiescence_service);
    ev_fault_registry_init(&graph->faults);
    ev_metric_registry_init(&graph->metrics);
    ev_trace_ring_init(&graph->trace_ring);
    ev_active_route_table_init(&graph->active_routes);
    ev_delivery_service_init(&graph->delivery_service, graph);

    graph->board_capabilities.configured = board_caps;
    graph->board_capabilities.active = board_caps;
    graph->board_capabilities.observed = 0U;
    graph->runtime_capabilities.configured = runtime_caps;
    graph->runtime_capabilities.active = runtime_caps;
    graph->runtime_capabilities.required = 0U;

    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        graph->actor_contexts[i].graph = graph;
        graph->actor_contexts[i].actor_id = (ev_actor_id_t)i;
        graph->lifecycle[i] = EV_ACTOR_STATE_UNINITIALIZED;
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
    builder->ports = *ports;
    builder->ports_set = 1U;
    builder->graph->ports = *ports;
    return EV_OK;
}

ev_result_t ev_runtime_builder_set_board_profile(ev_runtime_builder_t *builder, const ev_runtime_board_profile_t *profile)
{
    if ((builder == NULL) || (builder->graph == NULL) || (profile == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    builder->board_profile = *profile;
    builder->board_profile_set = 1U;
    builder->graph->board_profile = *profile;
    if (profile->configured_capabilities != 0U) {
        builder->board_caps = profile->configured_capabilities;
        builder->graph->board_capabilities.configured = profile->configured_capabilities;
    }
    if (profile->active_capabilities != 0U) {
        builder->graph->board_capabilities.active = profile->active_capabilities;
    }
    builder->graph->board_capabilities.observed = profile->observed_capabilities;
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
    builder->graph->instances[stored.actor_id] = stored;
    builder->graph->instance_bound[stored.actor_id] = 1U;
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
        instance.actor_context = &builder->graph->actor_contexts[actor_id];
        instance.actor_context_size = sizeof(builder->graph->actor_contexts[actor_id]);
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

ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder)
{
    size_t i;
    ev_result_t rc;

    if ((builder == NULL) || (builder->graph == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ev_active_route_table_init(&builder->graph->active_routes);
    for (i = 0U; i < ev_route_count(); ++i) {
        const ev_route_t *route = ev_route_at(i);
        ev_active_route_state_t state;
        ev_result_t reason = EV_OK;
        if (route == NULL) {
            continue;
        }
        state = ev_runtime_builder_classify_route(builder, route, &reason);
        rc = ev_active_route_table_add(&builder->graph->active_routes, route, state, reason);
        if (rc != EV_OK) {
            ev_route_t overflow_route = *route;
            (void)ev_active_route_table_add(&builder->graph->active_routes, &overflow_route, EV_ACTIVE_ROUTE_REJECTED_OVERFLOW, EV_ERR_FULL);
            builder->last_error = EV_ERR_FULL;
            return builder->last_error;
        }
        if (state == EV_ACTIVE_ROUTE_OPTIONAL_DISABLED) {
            (void)ev_metric_increment(&builder->graph->metrics, EV_METRIC_ROUTE_OPTIONAL_DISABLED, 1U);
        } else if (state != EV_ACTIVE_ROUTE_ENABLED) {
            (void)ev_metric_increment(&builder->graph->metrics, EV_METRIC_ROUTE_VALIDATION_REJECTED, 1U);
            if ((builder->route_validation_flags & EV_RUNTIME_ROUTE_VALIDATE_STRICT_MANDATORY) != 0U) {
                builder->last_error = reason;
                return reason;
            }
        }
    }
    builder->graph->active_routes_bound = 1U;
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

ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder)
{
    size_t i;

    if ((builder == NULL) || (builder->graph == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (builder->graph->active_routes_bound == 0U) {
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
            ev_result_t rc;

            if ((meta == NULL) || (descriptor == NULL)) {
                builder->last_error = EV_ERR_NOT_FOUND;
                return builder->last_error;
            }
            cap = ev_mailbox_kind_capacity(meta->mailbox_kind);
            if ((cap == 0U) || (cap > EV_RUNTIME_MAILBOX_CAPACITY_MAX)) {
                builder->last_error = EV_ERR_CONTRACT;
                return builder->last_error;
            }

            void *actor_context = &builder->graph->actor_contexts[i];
            ev_actor_handler_fn_t handler_fn = descriptor->handler_fn;
            if (builder->graph->instance_bound[i] != 0U) {
                const ev_actor_instance_descriptor_t *instance = &builder->graph->instances[i];
                if (instance->actor_context != NULL) {
                    actor_context = instance->actor_context;
                }
                if (instance->handler_fn != NULL) {
                    handler_fn = instance->handler_fn;
                }
            }
            rc = ev_mailbox_init(&builder->graph->mailboxes[i], meta->mailbox_kind, builder->graph->mailbox_storage[i], cap);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            rc = ev_actor_runtime_init(&builder->graph->actor_runtimes[i], actor_id, &builder->graph->mailboxes[i], handler_fn, actor_context);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            rc = ev_actor_registry_bind(&builder->graph->registry, &builder->graph->actor_runtimes[i]);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            builder->graph->actor_enabled[i] = 1U;
            builder->graph->descriptors[i] = descriptor;
            builder->graph->lifecycle[i] = EV_ACTOR_STATE_READY;
            if ((builder->graph->instance_bound[i] != 0U) && (builder->graph->instances[i].init_fn != NULL)) {
                rc = builder->graph->instances[i].init_fn(builder->graph->actor_runtimes[i].actor_context,
                                                         &builder->graph->ports,
                                                         &builder->graph->board_profile,
                                                         builder->graph->instances[i].user);
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
        ev_result_t sched_rc = ev_runtime_scheduler_init(&builder->graph->scheduler,
                                                         &builder->graph->registry,
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
    return ev_delivery_publish(&graph->delivery_service, msg, out_report);
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
    return ev_actor_registry_delivery(target_actor, &send_msg, &graph->registry);
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

const ev_active_route_table_t *ev_runtime_graph_active_routes(const ev_runtime_graph_t *graph)
{
    return (graph != NULL) ? &graph->active_routes : NULL;
}

ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id)
{
    if ((graph == NULL) || !ev_actor_id_is_valid(actor_id) || (graph->actor_enabled[actor_id] == 0U)) {
        return NULL;
    }
    return &graph->actor_runtimes[actor_id];
}

ev_result_t ev_runtime_graph_next_deadline_ms(const ev_runtime_graph_t *graph, uint32_t *out_deadline_ms)
{
    if ((graph == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return ev_timer_next_deadline_ms(&graph->timer_service, out_deadline_ms);
}

size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph)
{
    size_t i;
    size_t pending = 0U;

    if (graph == NULL) {
        return 0U;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        if (graph->actor_enabled[i] != 0U) {
            pending += ev_mailbox_count(&graph->mailboxes[i]);
        }
    }
    pending += ev_ingress_pending(&graph->ingress_service);
    pending += ev_timer_pending_count(&graph->timer_service);
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
        if (graph->actor_enabled[i] != 0U) {
            stats.actor_count++;
            stats.pending_actor_messages += ev_mailbox_count(&graph->mailboxes[i]);
        }
    }
    stats.pending_ingress_events = ev_ingress_pending(&graph->ingress_service);
    stats.pending_timers = ev_timer_pending_count(&graph->timer_service);
    stats.faults_emitted = graph->faults.emitted;
    stats.metrics_post_ok = graph->metrics.values[EV_METRIC_POST_OK];
    return stats;
}
