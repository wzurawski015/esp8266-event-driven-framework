# Warning exceptions

No source-level warning suppression was added for the framework layer.

The default host build preserves the original project warning policy:

```text
-std=c11 -Wall -Wextra -O0 -g0 -pedantic
```

The strict safety build is now explicit and host-only:

```text
-std=c17 -Wall -Wextra -Wpedantic -Werror
```

Run it with:

```sh
make host-strict-test
```

There are no broad `-Wno-error` or global warning suppressions in this patch. Future warning exceptions must be narrow, documented here, and tied to a specific compiler/toolchain limitation.
