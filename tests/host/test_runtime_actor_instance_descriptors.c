#include <assert.h>
#include <string.h>

#include "ev/runtime_graph.h"

static ev_result_t instance_init(void *actor_context, const ev_runtime_ports_t *ports, const ev_runtime_board_profile_t *profile, void *user)
{
    unsigned *called = (unsigned *)user;
    assert(actor_context != 0);
    assert(ports != 0);
    assert(profile != 0);
    (*called)++;
    return EV_OK;
}

int main(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_runtime_ports_t ports;
    ev_runtime_board_profile_t profile;
    ev_actor_instance_descriptor_t instance;
    const ev_actor_module_descriptor_t *module;
    ev_runtime_actor_context_t custom_context;
    unsigned init_called = 0U;

    memset(&ports, 0, sizeof(ports));
    memset(&profile, 0, sizeof(profile));
    profile.board_name = "host-test";
    profile.configured_capabilities = EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS;
    profile.active_capabilities = profile.configured_capabilities;

    assert(ev_runtime_builder_init(&builder, &graph, profile.configured_capabilities, profile.configured_capabilities) == EV_OK);
    assert(ev_runtime_builder_set_ports(&builder, &ports) == EV_OK);
    assert(ev_runtime_builder_set_board_profile(&builder, &profile) == EV_OK);

    module = ev_actor_module_find(ACT_FAULT);
    assert(module != 0);
    memset(&instance, 0, sizeof(instance));
    memset(&custom_context, 0, sizeof(custom_context));
    custom_context.graph = &graph;
    custom_context.actor_id = ACT_FAULT;
    instance.actor_id = ACT_FAULT;
    instance.module = module;
    instance.actor_context = &custom_context;
    instance.actor_context_size = sizeof(custom_context);
    instance.init_fn = instance_init;
    instance.user = &init_called;
    instance.required_capabilities = EV_CAP_FAULTS;

    assert(ev_actor_instance_validate(&instance) == EV_OK);
    assert(ev_runtime_builder_add_instance(&builder, &instance) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
    assert(init_called == 1U);
    assert(ev_runtime_graph_get_runtime(&graph, ACT_FAULT) != 0);
    assert(ev_runtime_graph_get_runtime(&graph, ACT_FAULT)->actor_context == &custom_context);
    return 0;
}
