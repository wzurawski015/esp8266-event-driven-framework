#ifndef EV_MSG_FLAGS_H
#define EV_MSG_FLAGS_H

#include <stdint.h>

/**
 * @brief Bit flags associated with a runtime message.
 */
typedef enum {
    EV_MSG_F_NONE = 0u,
    EV_MSG_F_DISPOSED = 1u << 0
} ev_msg_flags_t;

#endif /* EV_MSG_FLAGS_H */
