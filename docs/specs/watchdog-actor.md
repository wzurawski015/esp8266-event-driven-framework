# Watchdog Actor Contract

Status: implementation-ready policy with host-test coverage; ESP8266 hardware WDT feed still requires SDK/HIL verification.

The watchdog mechanism is separated from the runtime policy:

- `ports/include/ev/port_wdt.h` exposes only the platform mechanism: enable, feed, optional support and reset-reason queries.
- `core/src/ev_watchdog_actor.c` owns the policy and feeds only from the synchronous actor path.
- `app/ev_demo_app.c` owns the runtime liveness snapshot because it can observe the system pump, domain pumps, pending work and sleep arming state without coupling core to ESP8266.

Rules:

1. The watchdog is never fed from ISR, raw timer callbacks or a background platform task.
2. Unknown health is treated as unhealthy and does not feed the WDT.
3. The actor does not feed while sleep arming is active.
4. The actor feeds only when the HSHA graph reports progress and no bound domain reports a permanent stall.
5. If the watchdog actor is starved by a stuck pump or route failure, it naturally stops running and therefore stops feeding.
6. The ESP8266 adapter currently reports unsupported because no verified watchdog feed API is present in the repository headers. Future hardware enablement must replace that mechanism behind the existing port contract and prove it by HIL.
7. The accepted ESP8266 MISRA exception for boot-time FreeRTOS primitive allocation does not permit runtime heap use or watchdog hot-path allocation.

Suggested HIL scenarios:

- healthy graph feeds WDT;
- stalled pump stops feeding and reset reason is reported as watchdog after reboot;
- stuck hardware actor stops feeding;
- sleep arming stops feeding;
- reset reason is correlated with the boot diagnostic log.
