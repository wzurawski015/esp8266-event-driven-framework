#ifndef EV_BENCH_SUPPORT_H
#define EV_BENCH_SUPPORT_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "ev/result.h"

static inline void bench_require(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "BENCH_ERROR %s\n", message);
        exit(2);
    }
}

static inline uint64_t bench_now_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static inline void bench_require_ok(ev_result_t rc, const char *label)
{
    if (rc != EV_OK) {
        fprintf(stderr, "BENCH_ERROR %s rc=%d\n", label, (int)rc);
        exit(2);
    }
}

static inline void bench_emit_result(const char *name,
                                     uint64_t iterations,
                                     uint64_t logical_ops,
                                     uint64_t total_ns,
                                     uint64_t checksum)
{
    const uint64_t ns_per_iter = (iterations != 0U) ? (total_ns / iterations) : 0U;
    const uint64_t ns_per_op = (logical_ops != 0U) ? (total_ns / logical_ops) : 0U;
    printf("BENCH %s iterations=%" PRIu64 " logical_ops=%" PRIu64
           " total_ns=%" PRIu64 " ns_per_iter=%" PRIu64
           " ns_per_op=%" PRIu64 " checksum=%" PRIu64 "\n",
           name,
           iterations,
           logical_ops,
           total_ns,
           ns_per_iter,
           ns_per_op,
           checksum);
}

#endif /* EV_BENCH_SUPPORT_H */
