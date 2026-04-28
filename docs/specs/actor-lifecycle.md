# Actor lifecycle

Actors use a generic lifecycle model:

```c
EV_ACTOR_STATE_UNKNOWN
EV_ACTOR_STATE_UNINITIALIZED
EV_ACTOR_STATE_BOOTING
EV_ACTOR_STATE_READY
EV_ACTOR_STATE_DEGRADED
EV_ACTOR_STATE_OFFLINE
EV_ACTOR_STATE_SUSPENDING
EV_ACTOR_STATE_SLEEP_READY
EV_ACTOR_STATE_ERROR
```

Lifecycle events include `EV_ACTOR_READY`, `EV_ACTOR_DEGRADED`, `EV_ACTOR_OFFLINE`, `EV_ACTOR_SLEEP_READY`, and `EV_ACTOR_FAULT`. Device-specific ready events may remain for compatibility, but supervisor policy can now consume generic lifecycle events and capability snapshots.
