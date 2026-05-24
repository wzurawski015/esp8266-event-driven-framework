#include <assert.h>
#include <stddef.h>

#include "ev/actor_catalog.h"
#include "ev/actor_module.h"
#include "ev/mailbox_kind.h"

int main(void)
{
    const ev_actor_module_descriptor_t *table;
    size_t count = 0U;
    int seen[EV_ACTOR_COUNT] = { 0 };
    size_t i;

    table = ev_actor_module_table(&count);
    assert(table != 0);
    assert(count == EV_ACTOR_COUNT);
    assert(ev_actor_count() == EV_ACTOR_COUNT);

    for (i = 0U; i < count; ++i) {
        const ev_actor_module_descriptor_t *module = &table[i];
        assert(module->actor_id >= 0);
        assert(module->actor_id < EV_ACTOR_COUNT);
        assert(seen[module->actor_id] == 0);
        seen[module->actor_id] = 1;
    }

    for (i = 0U; i < EV_ACTOR_COUNT; ++i) {
        ev_actor_id_t actor_id = (ev_actor_id_t)i;
        const ev_actor_meta_t *meta = ev_actor_meta(actor_id);
        const ev_actor_module_descriptor_t *module = ev_actor_module_find(actor_id);

        assert(meta != 0);
        assert(module != 0);
        assert(module->actor_id == actor_id);
        assert(module->module_name != 0);
        assert(module->module_name[0] != '\0');
        assert(module->execution_domain == meta->execution_domain);
        assert(module->mailbox_capacity == ev_mailbox_kind_capacity(meta->mailbox_kind));
        assert(module->init_fn != 0);
        assert(module->bind_fn != 0);
        assert(module->quiescence_fn != 0);
        assert(module->stats_fn != 0);
        assert(module->lifecycle_fn != 0);
        assert(module->handler_fn != 0);
        assert(seen[actor_id] == 1);
    }

    return 0;
}
