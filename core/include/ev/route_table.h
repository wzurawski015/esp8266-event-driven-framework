#ifndef EV_ROUTE_TABLE_H
#define EV_ROUTE_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/actor_id.h"
#include "ev/event_id.h"

/**
 * @brief Per-route delivery policy.
 */
typedef enum {
    EV_ROUTE_QOS_CRITICAL = 0,
    EV_ROUTE_QOS_BEST_EFFORT = 1,
    EV_ROUTE_QOS_LOSSY = 2,
    EV_ROUTE_QOS_COALESCED = 3,
    EV_ROUTE_QOS_LATEST_ONLY = 4,
    EV_ROUTE_QOS_WAKEUP_CRITICAL = 5,
    EV_ROUTE_QOS_TELEMETRY = 6,
    EV_ROUTE_QOS_COMMAND = 7
} ev_route_qos_t;

/**
 * @brief Single static publish route entry.
 */
typedef struct {
    ev_event_id_t event_id;
    ev_actor_id_t target_actor;
    ev_route_qos_t qos;
    uint8_t priority;
    uint32_t flags;
} ev_route_t;

/**
 * @brief Contiguous route span for one event in the grouped route table.
 */
typedef struct {
    size_t start_index;
    size_t count;
} ev_route_span_t;

/**
 * @brief Return the number of declared static routes.
 *
 * @return Number of route entries.
 */
size_t ev_route_count(void);

/**
 * @brief Return a route entry by index.
 *
 * @param index Zero-based route index.
 * @return Route pointer or NULL when out of range.
 */
const ev_route_t *ev_route_at(size_t index);

/**
 * @brief Return the contiguous route span for a given event.
 *
 * @param event_id Event identifier.
 * @return Route span. Invalid or unrouted events return {0, 0}.
 */
ev_route_span_t ev_route_span_for_event(ev_event_id_t event_id);

/**
 * @brief Count static routes for a given event.
 *
 * @param event_id Event identifier.
 * @return Number of route entries for the event.
 */
size_t ev_route_count_for_event(ev_event_id_t event_id);

/**
 * @brief Test whether a route exists.
 */
bool ev_route_exists(ev_event_id_t event_id, ev_actor_id_t target_actor);

/**
 * @brief Return a stable text name for a QoS enumerator.
 */
const char *ev_route_qos_name(ev_route_qos_t qos);

#endif /* EV_ROUTE_TABLE_H */
