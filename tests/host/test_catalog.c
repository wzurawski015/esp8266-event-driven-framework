#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/event_catalog.h"
#include "ev/version.h"

int main(void)
{
    assert(strcmp(ev_version_string(), "0.1.0") == 0);

    assert(ev_event_count() > 0U);
    for (size_t i = 0; i < ev_event_count(); ++i) {
        const ev_event_meta_t *meta = ev_event_meta((ev_event_id_t)i);
        assert(meta != NULL);
        assert(meta->name != NULL);
        assert(meta->summary != NULL);
        assert(ev_event_name(meta->id) != NULL);
        assert(ev_payload_kind_name(meta->payload_kind) != NULL);
    }

    assert(ev_actor_count() > 0U);
    for (size_t i = 0; i < ev_actor_count(); ++i) {
        const ev_actor_meta_t *meta = ev_actor_meta((ev_actor_id_t)i);
        assert(meta != NULL);
        assert(meta->name != NULL);
        assert(meta->summary != NULL);
        assert(ev_actor_name(meta->id) != NULL);
        assert(ev_execution_domain_name(meta->execution_domain) != NULL);
        assert(ev_mailbox_kind_name(meta->mailbox_kind) != NULL);
        assert(meta->drain_budget > 0U);
        assert(ev_actor_default_drain_budget(meta->id) == meta->drain_budget);
    }

    puts("host smoke tests passed");
    return 0;
}
