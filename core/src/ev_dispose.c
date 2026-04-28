#include "ev/dispose.h"

#include <string.h>

ev_result_t ev_msg_dispose(ev_msg_t *msg)
{
    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (msg->cookie != EV_MSG_COOKIE) {
        ev_msg_reset(msg);
        msg->flags |= EV_MSG_F_DISPOSED;
        return EV_OK;
    }
    if (ev_msg_is_disposed(msg)) {
        return EV_OK;
    }

    if ((msg->storage == EV_MSG_STORAGE_EXTERNAL) && (msg->payload_size > 0U) &&
        (msg->payload.external.release_fn != NULL)) {
        msg->payload.external.release_fn(
            msg->payload.external.lifecycle_ctx,
            msg->payload.external.data,
            msg->payload_size);
    }

    memset(&msg->payload, 0, sizeof(msg->payload));
    msg->payload_size = 0U;
    msg->storage = EV_MSG_STORAGE_NONE;
    msg->flags |= EV_MSG_F_DISPOSED;
    return EV_OK;
}
