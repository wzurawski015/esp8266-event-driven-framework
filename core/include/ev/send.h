#ifndef EV_SEND_H
#define EV_SEND_H

#include "ev/delivery.h"
#include "ev/result.h"

/**
 * @brief Deliver a message directly to one target actor.
 *
 * @param target_actor Explicit target actor.
 * @param msg Message to deliver.
 * @param deliver Callback representing the mailbox insertion contract.
 * @param context Caller-provided callback context.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_send(ev_actor_id_t target_actor, const ev_msg_t *msg, ev_delivery_fn_t deliver, void *context);

#endif /* EV_SEND_H */
