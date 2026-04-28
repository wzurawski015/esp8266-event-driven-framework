#ifndef EV_ACTOR_RUNTIME_H
#define EV_ACTOR_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/actor_id.h"
#include "ev/delivery.h"
#include "ev/mailbox.h"
#include "ev/result.h"

/**
 * @brief Handler invoked when one message is drained from an actor mailbox.
 *
 * @param actor_context Caller-provided actor state.
 * @param msg Drained runtime envelope.
 * @return EV_OK on success or an error code.
 */
typedef ev_result_t (*ev_actor_handler_fn_t)(void *actor_context, const ev_msg_t *msg);

/**
 * @brief Cumulative delivery counters owned by one actor registry.
 */
typedef struct {
    uint32_t delivery_attempted;
    uint32_t delivery_succeeded;
    uint32_t delivery_failed;
    uint32_t delivery_target_missing;
    ev_actor_id_t last_target_actor;
    ev_result_t last_result;
} ev_actor_registry_stats_t;

/**
 * @brief Cumulative counters owned by one actor runtime.
 */
typedef struct {
    uint32_t enqueued;
    uint32_t enqueue_failed;
    uint32_t steps_ok;
    uint32_t steps_empty;
    uint32_t handler_errors;
    uint32_t dispose_errors;
    uint32_t pump_calls;
    uint32_t pump_budget_hits;
    size_t pending_high_watermark;
    size_t last_pump_budget;
    size_t last_pump_processed;
    ev_result_t last_result;
} ev_actor_runtime_stats_t;

/**
 * @brief Runtime wiring for one actor instance.
 */
typedef struct {
    ev_actor_id_t actor_id;
    ev_mailbox_t *mailbox;
    ev_actor_handler_fn_t handler;
    void *actor_context;
    ev_actor_runtime_stats_t stats;
} ev_actor_runtime_t;

/**
 * @brief Fixed-size registry mapping actor identifiers to runtimes.
 */
typedef struct {
    ev_actor_runtime_t *slots[EV_ACTOR_COUNT];
    ev_actor_registry_stats_t stats;
} ev_actor_registry_t;

/**
 * @brief Report produced by one bounded drain operation.
 */
typedef struct {
    size_t budget;
    size_t processed;
    size_t pending_before;
    size_t pending_after;
    bool exhausted_budget;
    ev_result_t stop_result;
} ev_actor_pump_report_t;

/**
 * @brief Reset one pump report to a stable empty state.
 *
 * @param report Report to clear.
 */
void ev_actor_pump_report_reset(ev_actor_pump_report_t *report);

/**
 * @brief Initialize one actor runtime.
 *
 * @param runtime Runtime to initialize.
 * @param actor_id Actor identifier.
 * @param mailbox Mailbox bound to this actor.
 * @param handler Message handler.
 * @param actor_context Caller-owned actor context.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_runtime_init(
    ev_actor_runtime_t *runtime,
    ev_actor_id_t actor_id,
    ev_mailbox_t *mailbox,
    ev_actor_handler_fn_t handler,
    void *actor_context);

/**
 * @brief Initialize an empty actor registry.
 *
 * @param registry Registry to initialize.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_registry_init(ev_actor_registry_t *registry);

/**
 * @brief Reset cumulative delivery counters for one registry.
 *
 * @param registry Registry to update.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_registry_reset_stats(ev_actor_registry_t *registry);

/**
 * @brief Return a stable pointer to registry delivery counters.
 *
 * @param registry Registry to inspect.
 * @return Pointer to counters or NULL when registry is NULL.
 */
const ev_actor_registry_stats_t *ev_actor_registry_stats(const ev_actor_registry_t *registry);

/**
 * @brief Bind one runtime into a registry slot.
 *
 * @param registry Registry to update.
 * @param runtime Runtime to bind.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_registry_bind(ev_actor_registry_t *registry, ev_actor_runtime_t *runtime);

/**
 * @brief Look up a runtime by actor identifier.
 *
 * @param registry Registry to inspect.
 * @param actor_id Actor identifier.
 * @return Bound runtime or NULL when absent.
 */
ev_actor_runtime_t *ev_actor_registry_find(ev_actor_registry_t *registry, ev_actor_id_t actor_id);

/**
 * @brief Delivery callback that enqueues into actor mailboxes.
 *
 * @param target_actor Target actor selected by send/publish.
 * @param msg Message to enqueue.
 * @param context Pointer to ev_actor_registry_t.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_registry_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context);

/**
 * @brief Drain at most one pending message from one actor runtime.
 *
 * The drained envelope is disposed after the handler returns.
 *
 * @param runtime Runtime to step.
 * @return EV_OK on success, EV_ERR_EMPTY when no message is pending, or an error code.
 */
ev_result_t ev_actor_runtime_step(ev_actor_runtime_t *runtime);

/**
 * @brief Drain up to @p budget messages from one actor runtime.
 *
 * This is the bounded-drain primitive used to prevent one logical actor from
 * monopolizing a cooperative loop. The function stops when one of three things
 * happens:
 *
 * - the mailbox becomes empty before the budget is consumed,
 * - the budget is consumed,
 * - handler or dispose fails.
 *
 * @param runtime Runtime to drain.
 * @param budget Maximum number of messages to process.
 * @param report Optional report receiving bounded-drain details.
 * @return EV_OK on success, EV_ERR_EMPTY when no work was pending, or an error code.
 */
ev_result_t ev_actor_runtime_pump(
    ev_actor_runtime_t *runtime,
    size_t budget,
    ev_actor_pump_report_t *report);

/**
 * @brief Drain one actor runtime using its SSOT-configured default budget.
 *
 * @param runtime Runtime to drain.
 * @param report Optional report receiving bounded-drain details.
 * @return EV_OK on success, EV_ERR_EMPTY when no work was pending, or an error code.
 */
ev_result_t ev_actor_runtime_pump_default(
    ev_actor_runtime_t *runtime,
    ev_actor_pump_report_t *report);

/**
 * @brief Return the default bounded-drain budget for one runtime.
 *
 * @param runtime Runtime to inspect.
 * @return Configured budget or 0 when runtime is NULL or invalid.
 */
size_t ev_actor_runtime_default_budget(const ev_actor_runtime_t *runtime);

/**
 * @brief Reset cumulative counters for one actor runtime.
 *
 * Queue state is left untouched. Only counters are cleared.
 *
 * @param runtime Runtime to update.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_actor_runtime_reset_stats(ev_actor_runtime_t *runtime);

/**
 * @brief Return a stable pointer to runtime counters.
 *
 * @param runtime Runtime to inspect.
 * @return Pointer to counters or NULL when runtime is NULL.
 */
const ev_actor_runtime_stats_t *ev_actor_runtime_stats(const ev_actor_runtime_t *runtime);

/**
 * @brief Return the number of pending messages for one runtime.
 *
 * @param runtime Runtime to inspect.
 * @return Pending message count.
 */
size_t ev_actor_runtime_pending(const ev_actor_runtime_t *runtime);

#endif /* EV_ACTOR_RUNTIME_H */
