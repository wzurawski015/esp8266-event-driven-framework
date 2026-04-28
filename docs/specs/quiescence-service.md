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
