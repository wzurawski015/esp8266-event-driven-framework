#ifndef EV_PUBLISH_H
#define EV_PUBLISH_H

#include <stddef.h>

#include "ev/delivery.h"
#include "ev/result.h"

/**
 * @brief Failure policy used by static publish fan-out.
 */
typedef enum {
    EV_PUBLISH_FAIL_FAST = 0,
    EV_PUBLISH_BEST_EFFORT = 1
} ev_publish_policy_t;

/**
 * @brief Delivery accounting produced by one publish attempt.
 */
typedef struct {
    size_t matched_routes;
    size_t attempted_deliveries;
    size_t delivered_count;
    size_t failed_count;
    ev_actor_id_t first_failed_actor;
    ev_result_t first_error;
} ev_publish_report_t;

/**
 * @brief Reset one publish report to the canonical zero state.
 *
 * @param report Report to reset.
 */
void ev_publish_report_reset(ev_publish_report_t *report);

/**
 * @brief Deliver a message through the static publish route table with an explicit policy.
 *
 * @param msg Message to publish. Its target actor must be EV_ACTOR_NONE.
 * @param deliver Callback representing the mailbox insertion contract.
 * @param context Caller-provided callback context.
 * @param policy Failure policy for multi-target fan-out.
 * @param report Optional output receiving detailed delivery accounting.
 * @return EV_OK on full success, EV_ERR_NOT_FOUND when no route exists,
 *         EV_ERR_PARTIAL for best-effort partial delivery, or another error code.
 */
ev_result_t ev_publish_ex(
    const ev_msg_t *msg,
    ev_delivery_fn_t deliver,
    void *context,
    ev_publish_policy_t policy,
    ev_publish_report_t *report);

/**
 * @brief Deliver a message through the static publish route table.
 *
 * This is the fail-fast wrapper used by the current contract-stage runtime.
 *
 * @param msg Message to publish. Its target actor must be EV_ACTOR_NONE.
 * @param deliver Callback representing the mailbox insertion contract.
 * @param context Caller-provided callback context.
 * @param delivered_count Optional output receiving the number of successful deliveries.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_publish(
    const ev_msg_t *msg,
    ev_delivery_fn_t deliver,
    void *context,
    size_t *delivered_count);

#endif /* EV_PUBLISH_H */
