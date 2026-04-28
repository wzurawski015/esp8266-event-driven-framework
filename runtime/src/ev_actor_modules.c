#include "ev/actor_module.h"

#include "ev/runtime_graph.h"

ev_result_t ev_default_module_init(ev_runtime_graph_t *graph, const ev_actor_module_descriptor_t *descriptor)
{
    if ((graph == NULL) || (descriptor == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    graph->runtime_capabilities.active |= descriptor->provided_capabilities;
    return EV_OK;
}

ev_result_t ev_default_module_bind(ev_runtime_graph_t *graph, const ev_actor_module_descriptor_t *descriptor)
{
    if ((graph == NULL) || (descriptor == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

ev_result_t ev_default_quiescence(void *actor_context, ev_quiescence_report_t *report)
{
    (void)actor_context;
    if (report == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

ev_result_t ev_default_module_stats(void *actor_context, void *out_stats)
{
    (void)actor_context;
    (void)out_stats;
    return EV_OK;
}

ev_result_t ev_default_lifecycle(void *actor_context, ev_actor_lifecycle_state_t old_state, ev_actor_lifecycle_state_t new_state)
{
    (void)actor_context;
    (void)old_state;
    (void)new_state;
    return EV_OK;
}

static const ev_actor_module_descriptor_t g_actor_modules[] = {
#define EV_ACTOR_MODULE(actor_id, module_name, req_board, req_runtime, provided, hw_mask, domain, mailbox_cap, fault_policy, route_flags, handler_fn) \
    { actor_id, module_name, (ev_capability_mask_t)(req_board), (ev_capability_mask_t)(req_runtime), (ev_capability_mask_t)(provided), (uint32_t)(hw_mask), domain, (size_t)(mailbox_cap), ev_default_module_init, ev_default_module_bind, ev_default_quiescence, ev_default_module_stats, ev_default_lifecycle, fault_policy, (uint32_t)(route_flags), handler_fn },
#include "modules.def"
#undef EV_ACTOR_MODULE
};

const ev_actor_module_descriptor_t *ev_actor_module_table(size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = sizeof(g_actor_modules) / sizeof(g_actor_modules[0]);
    }
    return g_actor_modules;
}

const ev_actor_module_descriptor_t *ev_actor_module_find(ev_actor_id_t actor_id)
{
    size_t i;
    for (i = 0U; i < (sizeof(g_actor_modules) / sizeof(g_actor_modules[0])); ++i) {
        if (g_actor_modules[i].actor_id == actor_id) {
            return &g_actor_modules[i];
        }
    }
    return NULL;
}

static ev_result_t ev_runtime_context_validate(ev_runtime_actor_context_t *ctx, const ev_msg_t *msg)
{
    if ((ctx == NULL) || (ctx->graph == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

ev_result_t ev_framework_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_runtime_actor_context_t *ctx = (ev_runtime_actor_context_t *)actor_context;
    ev_result_t rc = ev_runtime_context_validate(ctx, msg);
    if (rc != EV_OK) {
        return rc;
    }

    if (msg->event_id == EV_ACTOR_LIFECYCLE_CHANGED) {
        (void)ev_metric_increment(&ctx->graph->metrics, EV_METRIC_DELIVERY_OK, 1U);
    }
    return EV_OK;
}

ev_result_t ev_fault_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_runtime_actor_context_t *ctx = (ev_runtime_actor_context_t *)actor_context;
    ev_fault_payload_t fault;
    ev_result_t rc = ev_runtime_context_validate(ctx, msg);
    if (rc != EV_OK) {
        return rc;
    }

    if (msg->event_id == EV_FAULT_REPORTED) {
        fault.fault_id = EV_FAULT_ACTOR_HANDLER_FAILURE;
        fault.severity = EV_FAULT_SEV_WARNING;
        fault.source_actor = msg->source_actor;
        fault.triggering_event = msg->event_id;
        fault.source_module = 0U;
        fault.timestamp_ms = 0U;
        fault.arg0 = 0U;
        fault.arg1 = 0U;
        fault.counter = 0U;
        fault.flags = 0U;
        (void)ev_fault_emit(&ctx->graph->faults, &fault);
    }
    return EV_OK;
}

ev_result_t ev_metrics_actor_handle(void *actor_context, const ev_msg_t *msg)
{
    ev_runtime_actor_context_t *ctx = (ev_runtime_actor_context_t *)actor_context;
    ev_result_t rc = ev_runtime_context_validate(ctx, msg);
    if (rc != EV_OK) {
        return rc;
    }

    if (msg->event_id == EV_COMMAND_ACCEPTED) {
        (void)ev_metric_increment(&ctx->graph->metrics, EV_METRIC_COMMAND_ACCEPTED, 1U);
    } else if (msg->event_id == EV_COMMAND_REJECTED) {
        (void)ev_metric_increment(&ctx->graph->metrics, EV_METRIC_COMMAND_REJECTED, 1U);
    } else if (msg->event_id == EV_COMMAND_AUTH_FAILED) {
        (void)ev_metric_increment(&ctx->graph->metrics, EV_METRIC_COMMAND_AUTH_FAILED, 1U);
    }
    return EV_OK;
}
