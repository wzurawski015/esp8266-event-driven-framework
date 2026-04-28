#include "ev/runtime_graph.h"

#include <string.h>

#include "ev/actor_catalog.h"

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

    builder->requested[actor_id] = 1U;
    return EV_OK;
}

ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder)
{
    if (builder == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder)
{
    size_t i;

    if ((builder == NULL) || (builder->graph == NULL)) {
        return EV_ERR_INVALID_ARG;
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

            rc = ev_mailbox_init(&builder->graph->mailboxes[i], meta->mailbox_kind, builder->graph->mailbox_storage[i], cap);
            if (rc != EV_OK) {
                builder->last_error = rc;
                return rc;
            }
            rc = ev_actor_runtime_init(&builder->graph->actor_runtimes[i], actor_id, &builder->graph->mailboxes[i], descriptor->handler_fn, &builder->graph->actor_contexts[i]);
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

    return EV_OK;
}

ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id)
{
    if ((graph == NULL) || !ev_actor_id_is_valid(actor_id) || (graph->actor_enabled[actor_id] == 0U)) {
        return NULL;
    }
    return &graph->actor_runtimes[actor_id];
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
