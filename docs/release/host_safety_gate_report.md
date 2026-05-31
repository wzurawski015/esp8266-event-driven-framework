# Host safety gate report

## Scope

This patch adds host-only safety gates before zero-copy lease/view work. It does not change runtime semantics, route tables, mailbox capacities, actor behavior, SDK target semantics, HIL policy, deep sleep, QoS, trace timestamp, actor placement, or demo composition-root structure.

The gates are intentionally host-side. ESP8266 RTOS SDK builds can use a different compiler and do not inherit host `-Werror` or sanitizer flags.

## Strict C17 warning gate

Target:

```sh
make host-strict-test
```

Compiler standard and warning policy:

```text
-std=c17
-Wall
-Wextra
-Wpedantic
-Werror
```

The target rebuilds and runs the host test suite with `HOST_STRICT_CFLAGS`. It is not an echo-only PASS target.

## Sanitizer gate

Target:

```sh
make host-sanitize-test
```

Sanitizer flags:

```text
-fsanitize=address,undefined
-fno-omit-frame-pointer
-O1
-g
```

The target first performs a compiler capability check. If the host compiler cannot build a minimal ASAN/UBSAN binary, it prints `host-sanitize-test ENVIRONMENT_BLOCKED` and exits non-zero rather than claiming a false PASS.

## ThreadSanitizer target

Target:

```sh
make host-tsan-test
```

This is a separate optional target because the current host runtime is mostly single-threaded and some environments do not support TSAN. Unsupported toolchains print `ENVIRONMENT_BLOCKED`.

## clang-tidy target

Target:

```sh
make clang-tidy-gate
```

If `clang-tidy` is unavailable, the target reports `ENVIRONMENT_BLOCKED` and exits successfully because clang-tidy is not part of the mandatory `safety-gate` in this patch. If available, it scans portable layers and representative host tests.

## Aggregated safety gate

Target:

```sh
make safety-gate
```

Mandatory dependencies:

```text
host-strict-test
host-sanitize-test
```

`host-tsan-test` and `clang-tidy-gate` remain opt-in/report targets for this patch.

## Static contracts

`tools/audit/static_contracts.py` now checks that host strict C17 `-Werror` flags exist, sanitizer flags exist, sanitizer/TSAN targets remain host-only, unsupported tools are reported as `ENVIRONMENT_BLOCKED`, and `make safety-gate` depends on strict and ASAN/UBSAN host tests.

## Warning exceptions

No source-level warning suppression was added in this patch. Future warning exceptions must be narrow, documented in `docs/release/warning_exceptions.md`, and justified by compiler/toolchain behavior rather than convenience.

## Relevance to zero-copy work

The next zero-copy lease/view event contract will increase the importance of lifetime, bounds, aliasing, alignment, and ownership correctness. These host safety gates are the guardrail that should catch undefined behavior and memory misuse before that higher-risk work begins.
