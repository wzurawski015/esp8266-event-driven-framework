# Runtime builder

The runtime builder constructs an `ev_runtime_graph_t` from static actor module descriptors.

Public API:

```c
ev_result_t ev_runtime_graph_init(ev_runtime_graph_t *graph, uint32_t board_caps, uint32_t runtime_caps);
ev_result_t ev_runtime_builder_init(ev_runtime_builder_t *builder, ev_runtime_graph_t *graph, uint32_t board_caps, uint32_t runtime_caps);
ev_result_t ev_runtime_builder_add_module(ev_runtime_builder_t *builder, const ev_actor_module_descriptor_t *descriptor);
ev_result_t ev_runtime_builder_bind_routes(ev_runtime_builder_t *builder);
ev_result_t ev_runtime_builder_build(ev_runtime_builder_t *builder);
ev_actor_runtime_t *ev_runtime_graph_get_runtime(ev_runtime_graph_t *graph, ev_actor_id_t actor_id);
size_t ev_runtime_graph_pending(const ev_runtime_graph_t *graph);
ev_result_t ev_runtime_graph_stats(const ev_runtime_graph_t *graph, ev_runtime_graph_stats_t *out_stats);
```

The builder initializes static mailbox storage, actor runtimes, module callbacks, diagnostic services, timer service, ingress service, trace ring, power manager, and route delivery state. Capability mismatches are rejected deterministically with `EV_ERR_NO_CAPABILITY`.

## Runtime graph preparation API

The builder now supports injected runtime ports, board-profile snapshots and
concrete actor instance descriptors. `ev_runtime_builder_add_module()` remains a
compatibility wrapper; new framework applications should prefer concrete actor
instances once the demo migration begins.
