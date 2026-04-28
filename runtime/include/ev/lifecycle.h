#ifndef EV_LIFECYCLE_H
#define EV_LIFECYCLE_H

#include <stdint.h>
#include "ev/actor_id.h"

typedef enum {
    EV_ACTOR_STATE_UNKNOWN = 0,
    EV_ACTOR_STATE_UNINITIALIZED,
    EV_ACTOR_STATE_BOOTING,
    EV_ACTOR_STATE_READY,
    EV_ACTOR_STATE_DEGRADED,
    EV_ACTOR_STATE_OFFLINE,
    EV_ACTOR_STATE_SUSPENDING,
    EV_ACTOR_STATE_SLEEP_READY,
    EV_ACTOR_STATE_ERROR
} ev_actor_lifecycle_state_t;

typedef struct {
    ev_actor_id_t actor_id;
    ev_actor_lifecycle_state_t old_state;
    ev_actor_lifecycle_state_t new_state;
    uint32_t reason;
} ev_actor_lifecycle_payload_t;

#endif
