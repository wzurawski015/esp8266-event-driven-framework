#include "ev/actor_runtime.h"

#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/dispose.h"

static void ev_actor_runtime_record_pending_high_watermark(ev_actor_runtime_t *runtime)
{
    size_t pending;

    if ((runtime == NULL) || (runtime->mailbox == NULL)) {
        return;
    }

    pending = ev_mailbox_count(runtime->mailbox);
    if (pending > runtime->stats.pending_high_watermark) {
        runtime->stats.pending_high_watermark = pending;
    }
}

static ev_result_t ev_actor_runtime_process_one(ev_actor_runtime_t *runtime)
{
    ev_result_t rc;
    ev_result_t dispose_rc;
    ev_msg_t msg = {0};

    rc = ev_mailbox_pop(runtime->mailbox, &msg);
    if (rc != EV_OK) {
        runtime->stats.last_result = rc;
        return rc;
    }

    rc = runtime->handler(runtime->actor_context, &msg);
    if (rc != EV_OK) {
        ++runtime->stats.handler_errors;
    }

    dispose_rc = ev_msg_dispose(&msg);
    if (dispose_rc != EV_OK) {
        ++runtime->stats.dispose_errors;
        if (rc == EV_OK) {
            rc = dispose_rc;
        }
    }

    if (rc == EV_OK) {
        ++runtime->stats.steps_ok;
    }
    runtime->stats.last_result = rc;
    return rc;
}

void ev_actor_pump_report_reset(ev_actor_pump_report_t *report)
{
    if (report == NULL) {
        return;
    }

    memset(report, 0, sizeof(*report));
    report->stop_result = EV_OK;
}

ev_result_t ev_actor_runtime_init(
    ev_actor_runtime_t *runtime,
    ev_actor_id_t actor_id,
    ev_mailbox_t *mailbox,
    ev_actor_handler_fn_t handler,
    void *actor_context)
{
    const ev_actor_meta_t *meta;

    if ((runtime == NULL) || (mailbox == NULL) || (handler == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(actor_id)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    meta = ev_actor_meta(actor_id);
    if (meta == NULL) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (mailbox->kind != meta->mailbox_kind) {
        return EV_ERR_CONTRACT;
    }
    if (meta->drain_budget == 0U) {
        return EV_ERR_CONTRACT;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->actor_id = actor_id;
    runtime->mailbox = mailbox;
    runtime->handler = handler;
    runtime->actor_context = actor_context;
    runtime->stats.last_result = EV_OK;
    return EV_OK;
}

ev_result_t ev_actor_registry_init(ev_actor_registry_t *registry)
{
    if (registry == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(registry, 0, sizeof(*registry));
    registry->stats.last_target_actor = EV_ACTOR_NONE;
    registry->stats.last_result = EV_OK;
    return EV_OK;
}

ev_result_t ev_actor_registry_reset_stats(ev_actor_registry_t *registry)
{
    if (registry == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&registry->stats, 0, sizeof(registry->stats));
    registry->stats.last_target_actor = EV_ACTOR_NONE;
    registry->stats.last_result = EV_OK;
    return EV_OK;
}

const ev_actor_registry_stats_t *ev_actor_registry_stats(const ev_actor_registry_t *registry)
{
    return (registry != NULL) ? &registry->stats : NULL;
}

ev_actor_runtime_t *ev_actor_registry_find(ev_actor_registry_t *registry, ev_actor_id_t actor_id)
{
    if ((registry == NULL) || !ev_actor_id_is_valid(actor_id)) {
        return NULL;
    }

    return registry->slots[actor_id];
}

ev_result_t ev_actor_registry_bind(ev_actor_registry_t *registry, ev_actor_runtime_t *runtime)
{
    if ((registry == NULL) || (runtime == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(runtime->actor_id)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (registry->slots[runtime->actor_id] != NULL) {
        return EV_ERR_STATE;
    }

    registry->slots[runtime->actor_id] = runtime;
    return EV_OK;
}

ev_result_t ev_actor_registry_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    ev_actor_registry_t *registry = (ev_actor_registry_t *)context;
    ev_actor_runtime_t *runtime;
    ev_result_t rc;

    if (registry == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++registry->stats.delivery_attempted;
    registry->stats.last_target_actor = target_actor;

    runtime = ev_actor_registry_find(registry, target_actor);
    if (runtime == NULL) {
        ++registry->stats.delivery_failed;
        ++registry->stats.delivery_target_missing;
        registry->stats.last_result = EV_ERR_NOT_FOUND;
        return EV_ERR_NOT_FOUND;
    }

    rc = ev_mailbox_push(runtime->mailbox, msg);
    runtime->stats.last_result = rc;
    if (rc == EV_OK) {
        ++registry->stats.delivery_succeeded;
        registry->stats.last_result = EV_OK;
        ++runtime->stats.enqueued;
        ev_actor_runtime_record_pending_high_watermark(runtime);
        return EV_OK;
    }

    ++registry->stats.delivery_failed;
    registry->stats.last_result = rc;
    ++runtime->stats.enqueue_failed;
    return rc;
}

ev_result_t ev_actor_runtime_step(ev_actor_runtime_t *runtime)
{
    if ((runtime == NULL) || (runtime->mailbox == NULL) || (runtime->handler == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_mailbox_is_empty(runtime->mailbox)) {
        ++runtime->stats.steps_empty;
        runtime->stats.last_result = EV_ERR_EMPTY;
        return EV_ERR_EMPTY;
    }

    return ev_actor_runtime_process_one(runtime);
}

size_t ev_actor_runtime_default_budget(const ev_actor_runtime_t *runtime)
{
    return ((runtime != NULL) && ev_actor_id_is_valid(runtime->actor_id))
               ? ev_actor_default_drain_budget(runtime->actor_id)
               : 0U;
}

ev_result_t ev_actor_runtime_pump(
    ev_actor_runtime_t *runtime,
    size_t budget,
    ev_actor_pump_report_t *report)
{
    ev_result_t rc = EV_OK;
    ev_actor_pump_report_t local_report;

    if ((runtime == NULL) || (runtime->mailbox == NULL) || (runtime->handler == NULL) || (budget == 0U)) {
        return EV_ERR_INVALID_ARG;
    }

    if (report == NULL) {
        report = &local_report;
    }
    ev_actor_pump_report_reset(report);
    report->budget = budget;
    report->pending_before = ev_actor_runtime_pending(runtime);

    ++runtime->stats.pump_calls;
    runtime->stats.last_pump_budget = budget;
    runtime->stats.last_pump_processed = 0U;

    if (report->pending_before == 0U) {
        ++runtime->stats.steps_empty;
        runtime->stats.last_result = EV_ERR_EMPTY;
        report->stop_result = EV_ERR_EMPTY;
        return EV_ERR_EMPTY;
    }

    while ((report->processed < budget) && !ev_mailbox_is_empty(runtime->mailbox)) {
        rc = ev_actor_runtime_process_one(runtime);
        ++report->processed;
        if (rc != EV_OK) {
            report->pending_after = ev_actor_runtime_pending(runtime);
            report->stop_result = rc;
            runtime->stats.last_pump_processed = report->processed;
            return rc;
        }
    }

    report->pending_after = ev_actor_runtime_pending(runtime);
    runtime->stats.last_pump_processed = report->processed;

    if (report->processed < budget) {
        report->stop_result = EV_ERR_EMPTY;
        return EV_OK;
    }

    if (report->pending_after > 0U) {
        report->exhausted_budget = true;
        ++runtime->stats.pump_budget_hits;
    }

    report->stop_result = EV_OK;
    return EV_OK;
}

ev_result_t ev_actor_runtime_pump_default(
    ev_actor_runtime_t *runtime,
    ev_actor_pump_report_t *report)
{
    size_t budget = ev_actor_runtime_default_budget(runtime);
    if (budget == 0U) {
        return EV_ERR_CONTRACT;
    }
    return ev_actor_runtime_pump(runtime, budget, report);
}

ev_result_t ev_actor_runtime_reset_stats(ev_actor_runtime_t *runtime)
{
    if (runtime == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    memset(&runtime->stats, 0, sizeof(runtime->stats));
    runtime->stats.last_result = EV_OK;
    return EV_OK;
}

const ev_actor_runtime_stats_t *ev_actor_runtime_stats(const ev_actor_runtime_t *runtime)
{
    return (runtime != NULL) ? &runtime->stats : NULL;
}

size_t ev_actor_runtime_pending(const ev_actor_runtime_t *runtime)
{
    return ((runtime != NULL) && (runtime->mailbox != NULL))
               ? ev_mailbox_count(runtime->mailbox)
               : 0U;
}
