#include "ev_atnel_air_app.h"

ev_result_t ev_atnel_air_app_build(ev_runtime_graph_t *graph)
{
    ev_runtime_builder_t builder;
    const ev_actor_module_descriptor_t *modules;
    size_t count;
    size_t i;
    ev_result_t rc;

    rc = ev_runtime_builder_init(&builder, graph, EV_CAP_ALL_KNOWN, EV_CAP_ALL_KNOWN);
    if (rc != EV_OK) {
        return rc;
    }

    modules = ev_actor_module_table(&count);
    for (i = 0U; i < count; ++i) {
        rc = ev_runtime_builder_add_module(&builder, modules[i].actor_id);
        if (rc != EV_OK) {
            return rc;
        }
    }
    rc = ev_runtime_builder_bind_routes(&builder);
    if (rc != EV_OK) {
        return rc;
    }
    return ev_runtime_builder_build(&builder);
}
