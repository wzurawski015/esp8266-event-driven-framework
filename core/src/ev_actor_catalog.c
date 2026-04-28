#include "ev/actor_catalog.h"

#include "ev/compiler.h"

static const ev_actor_meta_t k_actor_catalog[] = {
#define EV_ACTOR(name, execution_domain, mailbox_kind, drain_budget, summary) \
    { name, #name, execution_domain, mailbox_kind, drain_budget, summary },
#include "actors.def"
#undef EV_ACTOR
};

EV_STATIC_ASSERT(EV_ARRAY_LEN(k_actor_catalog) == EV_ACTOR_COUNT, "actor catalog mismatch");

size_t ev_actor_count(void)
{
    return EV_ARRAY_LEN(k_actor_catalog);
}

const ev_actor_meta_t *ev_actor_meta(ev_actor_id_t id)
{
    if ((id < 0) || ((size_t)id >= EV_ARRAY_LEN(k_actor_catalog))) {
        return NULL;
    }

    return &k_actor_catalog[id];
}

bool ev_actor_id_is_valid(ev_actor_id_t id)
{
    return ev_actor_meta(id) != NULL;
}

const char *ev_actor_name(ev_actor_id_t id)
{
    const ev_actor_meta_t *meta = ev_actor_meta(id);
    return (meta != NULL) ? meta->name : NULL;
}

size_t ev_actor_default_drain_budget(ev_actor_id_t id)
{
    const ev_actor_meta_t *meta = ev_actor_meta(id);
    return (meta != NULL) ? meta->drain_budget : 0U;
}

const char *ev_execution_domain_name(ev_execution_domain_t domain)
{
    switch (domain) {
    case EV_DOMAIN_ISR:
        return "EV_DOMAIN_ISR";
    case EV_DOMAIN_FAST_LOOP:
        return "EV_DOMAIN_FAST_LOOP";
    case EV_DOMAIN_SLOW_IO:
        return "EV_DOMAIN_SLOW_IO";
    case EV_DOMAIN_NETWORK:
        return "EV_DOMAIN_NETWORK";
    default:
        return "EV_DOMAIN_UNKNOWN";
    }
}

const char *ev_mailbox_kind_name(ev_mailbox_kind_t kind)
{
    switch (kind) {
    case EV_MAILBOX_FIFO_8:
        return "EV_MAILBOX_FIFO_8";
    case EV_MAILBOX_FIFO_16:
        return "EV_MAILBOX_FIFO_16";
    case EV_MAILBOX_MAILBOX_1:
        return "EV_MAILBOX_MAILBOX_1";
    case EV_MAILBOX_LOSSY_RING_8:
        return "EV_MAILBOX_LOSSY_RING_8";
    case EV_MAILBOX_COALESCED_FLAG:
        return "EV_MAILBOX_COALESCED_FLAG";
    default:
        return "EV_MAILBOX_UNKNOWN";
    }
}
