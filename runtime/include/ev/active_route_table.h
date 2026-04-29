#ifndef EV_ACTIVE_ROUTE_TABLE_H
#define EV_ACTIVE_ROUTE_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "ev/result.h"
#include "ev/route_table.h"

#ifndef EV_ACTIVE_ROUTE_TABLE_CAPACITY
#define EV_ACTIVE_ROUTE_TABLE_CAPACITY 96U
#endif

#define EV_RUNTIME_ROUTE_VALIDATE_STRICT_MANDATORY 0x00000001UL

typedef enum {
    EV_ACTIVE_ROUTE_ENABLED = 0,
    EV_ACTIVE_ROUTE_OPTIONAL_DISABLED,
    EV_ACTIVE_ROUTE_REJECTED_INVALID_EVENT,
    EV_ACTIVE_ROUTE_REJECTED_INVALID_ACTOR,
    EV_ACTIVE_ROUTE_REJECTED_MISSING_MANDATORY_ACTOR,
    EV_ACTIVE_ROUTE_REJECTED_QOS_CONFLICT,
    EV_ACTIVE_ROUTE_REJECTED_OVERFLOW
} ev_active_route_state_t;

typedef struct {
    ev_route_t route;
    ev_active_route_state_t state;
    ev_result_t reason;
} ev_active_route_t;

typedef struct {
    ev_active_route_t entries[EV_ACTIVE_ROUTE_TABLE_CAPACITY];
    size_t count;
    size_t active_count;
    size_t optional_disabled_count;
    size_t rejected_count;
} ev_active_route_table_t;

void ev_active_route_table_init(ev_active_route_table_t *table);
ev_result_t ev_active_route_table_add(ev_active_route_table_t *table, const ev_route_t *route, ev_active_route_state_t state, ev_result_t reason);
const ev_active_route_t *ev_active_route_at(const ev_active_route_table_t *table, size_t index);
const char *ev_active_route_state_name(ev_active_route_state_t state);
int ev_route_qos_is_valid(ev_route_qos_t qos);

#endif /* EV_ACTIVE_ROUTE_TABLE_H */
