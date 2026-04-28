#ifndef EV_DELIVERY_H
#define EV_DELIVERY_H

#include "ev/actor_id.h"
#include "ev/msg.h"
#include "ev/result.h"

/**
 * @brief Callback used by the contract-stage delivery functions.
 *
 * @param target_actor Delivery target selected by the caller or static routes.
 * @param msg Immutable runtime message.
 * @param context Caller-provided callback context.
 * @return EV_OK on success or an error code.
 */
typedef ev_result_t (*ev_delivery_fn_t)(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context);

#endif /* EV_DELIVERY_H */
