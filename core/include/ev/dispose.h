#ifndef EV_DISPOSE_H
#define EV_DISPOSE_H

#include "ev/msg.h"
#include "ev/result.h"

/**
 * @brief Dispose a message and release any externally owned payload.
 *
 * Safe on zero-initialized messages. Repeated disposal is a no-op after the
 * first effective release.
 *
 * @param msg Message to dispose.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_dispose(ev_msg_t *msg);

#endif /* EV_DISPOSE_H */
