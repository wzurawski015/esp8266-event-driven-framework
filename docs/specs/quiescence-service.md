# Quiescence service

The quiescence service reports whether the graph is safe to idle or enter a sleep path.

Report fields include:

- pending actor messages,
- pending ingress events,
- due timers,
- busy actor mask,
- sleep blocker actor mask,
- pending trace records,
- pending fault records,
- next deadline,
- reason string.

Actors may expose an `ev_actor_quiescence_fn_t` callback through their module descriptor. Application power code no longer needs to inspect device-specific fields such as OLED flush state or DS18B20 conversion state directly.

## Demo runtime migration note

The demo application sleep guard delegates to `ev_runtime_is_quiescent_at()` and actor quiescence callbacks. OLED flush and DS18B20 conversion state are reported through callbacks instead of direct sleep-guard inspection of actor internals. Log flush pending integration remains a post-migration item.

## Demo actor blockers

Demo actor-specific sleep blockers are reported through actor quiescence callbacks. OLED pending flush and DS18B20 conversion state are mapped into `sleep_blocker_actor_mask`; demo power code must not read those actor internals directly when making the sleep decision.
