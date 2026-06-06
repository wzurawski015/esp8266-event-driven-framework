#ifndef EV_ROUTE_TEST_UTILS_H
#define EV_ROUTE_TEST_UTILS_H

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "ev/route_table_generated.h"

#if defined(__GNUC__) || defined(__clang__)
#define EV_TEST_ROUTE_UNUSED __attribute__((unused))
#else
#define EV_TEST_ROUTE_UNUSED
#endif

static inline size_t EV_TEST_ROUTE_UNUSED ev_test_route_count_for_event(ev_event_id_t event_id)
{
    size_t count = 0U;
    for (size_t i = 0U; i < EV_ROUTE_TABLE_GENERATED_COUNT; ++i) {
        if (k_ev_route_table_generated[i].event_id == event_id) {
            ++count;
        }
    }
    return count;
}

static inline ev_actor_id_t EV_TEST_ROUTE_UNUSED ev_test_last_route_target_for_event(ev_event_id_t event_id)
{
    ev_actor_id_t target = EV_ACTOR_NONE;
    for (size_t i = 0U; i < EV_ROUTE_TABLE_GENERATED_COUNT; ++i) {
        if (k_ev_route_table_generated[i].event_id == event_id) {
            target = k_ev_route_table_generated[i].target_actor;
        }
    }
    return target;
}

static inline bool EV_TEST_ROUTE_UNUSED ev_test_actor_is_in_set(
    ev_actor_id_t actor,
    const ev_actor_id_t *actors,
    size_t actor_count)
{
    for (size_t i = 0U; i < actor_count; ++i) {
        if (actors[i] == actor) {
            return true;
        }
    }
    return false;
}

static inline void EV_TEST_ROUTE_UNUSED ev_test_assert_event_targets_are_bound(
    ev_event_id_t event_id,
    const ev_actor_id_t *bound_actors,
    size_t bound_actor_count)
{
    for (size_t i = 0U; i < EV_ROUTE_TABLE_GENERATED_COUNT; ++i) {
        if (k_ev_route_table_generated[i].event_id == event_id) {
            assert(ev_test_actor_is_in_set(k_ev_route_table_generated[i].target_actor, bound_actors, bound_actor_count));
        }
    }
}

#endif /* EV_ROUTE_TEST_UTILS_H */
