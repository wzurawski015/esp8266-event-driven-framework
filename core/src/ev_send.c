#include "ev/send.h"

#include "ev/actor_catalog.h"
#include "ev/msg.h"

ev_result_t ev_send(ev_actor_id_t target_actor, const ev_msg_t *msg, ev_delivery_fn_t deliver, void *context)
{
    ev_result_t rc;

    if ((msg == NULL) || (deliver == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((target_actor == EV_ACTOR_NONE) || !ev_actor_id_is_valid(target_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    rc = ev_msg_validate(msg);
    if (rc != EV_OK) {
        return rc;
    }
    if (msg->target_actor != target_actor) {
        return EV_ERR_CONTRACT;
    }

    return deliver(target_actor, msg, context);
}
