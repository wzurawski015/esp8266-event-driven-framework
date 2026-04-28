#include "ev/event_catalog.h"

#include "ev/compiler.h"

static const ev_event_meta_t k_event_catalog[] = {
#define EV_EVENT(name, payload_kind, summary) { name, #name, payload_kind, summary },
#include "events.def"
#undef EV_EVENT
};

EV_STATIC_ASSERT(EV_ARRAY_LEN(k_event_catalog) == EV_EVENT_COUNT, "event catalog mismatch");

size_t ev_event_count(void)
{
    return EV_ARRAY_LEN(k_event_catalog);
}

const ev_event_meta_t *ev_event_meta(ev_event_id_t id)
{
    if ((id < 0) || ((size_t)id >= EV_ARRAY_LEN(k_event_catalog))) {
        return NULL;
    }

    return &k_event_catalog[id];
}

bool ev_event_id_is_valid(ev_event_id_t id)
{
    return ev_event_meta(id) != NULL;
}

const char *ev_event_name(ev_event_id_t id)
{
    const ev_event_meta_t *meta = ev_event_meta(id);
    return (meta != NULL) ? meta->name : NULL;
}

const char *ev_payload_kind_name(ev_payload_kind_t kind)
{
    switch (kind) {
    case EV_PAYLOAD_INLINE:
        return "EV_PAYLOAD_INLINE";
    case EV_PAYLOAD_COPY_FIXED:
        return "EV_PAYLOAD_COPY_FIXED";
    case EV_PAYLOAD_LEASE:
        return "EV_PAYLOAD_LEASE";
    case EV_PAYLOAD_STREAM_VIEW:
        return "EV_PAYLOAD_STREAM_VIEW";
    default:
        return "EV_PAYLOAD_UNKNOWN";
    }
}
