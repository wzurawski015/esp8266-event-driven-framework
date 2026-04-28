#ifndef EV_EVENT_ID_H
#define EV_EVENT_ID_H

#include "ev/payload_kind.h"

/**
 * @brief Event identifiers generated from config/events.def.
 */
typedef enum {
#define EV_EVENT(name, payload_kind, summary) name,
#include "events.def"
#undef EV_EVENT
    EV_EVENT_COUNT
} ev_event_id_t;

#endif /* EV_EVENT_ID_H */
