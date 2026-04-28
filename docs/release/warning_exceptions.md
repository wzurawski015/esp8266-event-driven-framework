# Warning exceptions

No source-level warning suppression was added for the new framework layer.

The host build preserves the original project warning policy:

```text
-std=c11 -Wall -Wextra -O0 -g0 -pedantic
```

Global `-Werror` was not enabled for the full preserved legacy codebase to avoid changing the original acceptance behavior.
