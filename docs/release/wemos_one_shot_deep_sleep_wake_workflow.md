# Wemos one-shot deep-sleep/wake workflow

Strict Wemos deep-sleep/wake evidence is captured separately from smoke-only evidence.

Run only with explicit operator intent:

```sh
export EV_WEMOS_ONE_SHOT_DEEPSLEEP=1
export EV_WEMOS_ONE_SHOT_MONITOR=1
export EV_HIL_ALLOW_MONITOR=1
make wemos-one-shot-deepsleep-evidence-capture
make wemos-one-shot-deepsleep-evidence-gate
```

Deep-sleep evidence must use strict marker proof. Runtime-alive fallback is never accepted for deep-sleep/wake PASS.
