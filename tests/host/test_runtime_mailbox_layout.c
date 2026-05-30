#include <assert.h>
#include <stddef.h>

#include "ev/actor_catalog.h"
#include "ev/actor_mailbox_layout_generated.h"
#include "ev/mailbox.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"

static void assert_layout_contract(void)
{
    unsigned char occupied[EV_RUNTIME_MAILBOX_TOTAL_CAPACITY] = { 0U };
    size_t expected_offset = 0U;
    size_t configured_sum = 0U;
    size_t i;

    assert(EV_ACTOR_MAILBOX_LAYOUT_GENERATED_COUNT == EV_ACTOR_COUNT);
    assert(EV_RUNTIME_MAILBOX_TOTAL_CAPACITY == 144U);
    assert(ev_runtime_graph_configured_mailbox_slots() == EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(ev_runtime_graph_configured_mailbox_bytes() ==
           (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY * sizeof(ev_msg_t)));

    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        ev_actor_id_t actor_id = (ev_actor_id_t)i;
        const ev_actor_meta_t *meta = ev_actor_meta(actor_id);
        ev_actor_mailbox_layout_entry_t layout;
        size_t end;
        size_t j;

        assert(meta != 0);
        assert(ev_actor_mailbox_layout_lookup(actor_id, &layout) == 1);
        assert(layout.actor_id == actor_id);
        assert(layout.mailbox_kind == meta->mailbox_kind);
        assert(layout.capacity == ev_mailbox_kind_capacity(meta->mailbox_kind));
        assert(layout.capacity != 0U);
        assert(layout.capacity <= EV_RUNTIME_MAILBOX_CAPACITY_MAX);
        assert((layout.capacity & (layout.capacity - 1U)) == 0U);
        assert(layout.offset == expected_offset);
        assert(layout.capacity <= (EV_RUNTIME_MAILBOX_TOTAL_CAPACITY - layout.offset));

        end = layout.offset + layout.capacity;
        for (j = layout.offset; j < end; ++j) {
            assert(occupied[j] == 0U);
            occupied[j] = 1U;
        }

        configured_sum += layout.capacity;
        expected_offset = end;

        if (actor_id == ACT_STREAM) {
            assert(layout.mailbox_kind == EV_MAILBOX_FIFO_16);
            assert(layout.capacity == 16U);
        } else {
            assert(layout.mailbox_kind == EV_MAILBOX_FIFO_8);
            assert(layout.capacity == 8U);
        }
    }

    assert(configured_sum == EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(expected_offset == EV_RUNTIME_MAILBOX_TOTAL_CAPACITY);
    assert(ev_actor_mailbox_layout_lookup(EV_ACTOR_NONE, &(ev_actor_mailbox_layout_entry_t){ 0 }) == 0);
}

static void assert_builder_uses_generated_offsets(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_actor_mailbox_layout_entry_t boot_layout;
    ev_actor_mailbox_layout_entry_t stream_layout;

    assert(ev_actor_mailbox_layout_lookup(ACT_BOOT, &boot_layout) == 1);
    assert(ev_actor_mailbox_layout_lookup(ACT_STREAM, &stream_layout) == 1);

    assert(ev_runtime_builder_init(&builder, &graph, 0U, 0U) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_BOOT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_STREAM) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);

    {
        size_t boot_offset = 0U;
        size_t stream_offset = 0U;

        assert(ev_runtime_graph_actor_mailbox_offset(&graph, ACT_BOOT, &boot_offset) == EV_OK);
        assert(ev_runtime_graph_actor_mailbox_offset(&graph, ACT_STREAM, &stream_offset) == EV_OK);
        assert(boot_offset == boot_layout.offset);
        assert(ev_runtime_graph_actor_mailbox_capacity(&graph, ACT_BOOT) == boot_layout.capacity);
        assert(stream_offset == stream_layout.offset);
        assert(ev_runtime_graph_actor_mailbox_capacity(&graph, ACT_STREAM) == 16U);
        assert(ev_runtime_graph_actor_mailbox_capacity(&graph, ACT_STREAM) == stream_layout.capacity);
    }
}

int main(void)
{
    assert_layout_contract();
    assert_builder_uses_generated_offsets();
    return 0;
}
