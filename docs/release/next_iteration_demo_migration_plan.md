# Next iteration: demo migration to runtime_graph

Recommended next commit:

```text
refactor(app): migrate demo app to runtime_graph without losing behavior
```

Required acceptance criteria:

- `apps/demo/include/ev/demo_app.h` no longer owns per-actor mailboxes.
- `apps/demo/include/ev/demo_app.h` no longer owns per-actor actor runtimes.
- `apps/demo/ev_demo_app.c` no longer manually binds every actor into a registry.
- `ev_demo_app_poll()` delegates scheduler work to `runtime_graph`.
- disabled NET/WDT route behavior remains counted and safe.
- fairness, starvation, BSP profile, sleep quiescence and fault tests pass.
