#ifndef EV_COMPILER_H
#define EV_COMPILER_H

#include <stddef.h>

#if defined(__GNUC__) || defined(__clang__)
#define EV_NODISCARD __attribute__((warn_unused_result))
#define EV_NONNULL(...) __attribute__((nonnull(__VA_ARGS__)))
#else
#define EV_NODISCARD
#define EV_NONNULL(...)
#endif

#define EV_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))
#define EV_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)

#endif /* EV_COMPILER_H */
