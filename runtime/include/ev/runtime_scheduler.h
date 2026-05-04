#ifndef EV_RUNTIME_SCHEDULER_H
#define EV_RUNTIME_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

#include "ev/actor_runtime.h"
#include "ev/domain_pump.h"
#include "ev/system_pump.h"

#define EV_RUNTIME_DOMAIN_MASK(domain_) (1UL << (uint32_t)(domain_))

typedef struct {
    ev_domain_pump_t domains[EV_DOMAIN_COUNT];
    ev_system_pump_t system;
    uint32_t active_domain_mask;
    uint32_t poll_calls;
    uint32_t partial_returns;
} ev_runtime_scheduler_t;

ev_result_t ev_runtime_scheduler_init(
    ev_runtime_scheduler_t *scheduler,
    ev_actor_registry_t *registry,
    uint32_t active_domain_mask);

ev_result_t ev_runtime_scheduler_poll_once(
    ev_runtime_scheduler_t *scheduler,
    size_t turn_budget,
    ev_system_pump_report_t *out_report);

size_t ev_runtime_scheduler_pending(const ev_runtime_scheduler_t *scheduler);

#endif /* EV_RUNTIME_SCHEDULER_H */
