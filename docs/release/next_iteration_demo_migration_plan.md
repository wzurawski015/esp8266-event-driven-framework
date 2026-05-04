# Next iteration: no-legacy framework hardening

The demo runtime-ownership migration has been applied. The next iteration is no
longer a migration from manual mailboxes/registry/pumps; it is hardening that
removes the remaining compatibility seams.

Required acceptance criteria:

- `apps/demo` does not access `app->graph.scheduler` or `app->graph.timer_service` directly.
- `ev_demo_app_poll()` delegates orchestration to reusable `runtime_loop`.
- production actor initialization does not pass `ev_demo_app_delivery`.
- route/delivery fault and metric emission is covered by tests.
- quiescence reports log pending state through a real log-port hook.
- static contracts hard-fail regression to demo-owned runtime internals.

Post-hardening work remains: SDK build matrix, HIL, Wemos board smoke and final
ESP8266 linker-map memory reporting.
