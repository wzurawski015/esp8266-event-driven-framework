#ifndef EV_POWER_MANAGER_H
#define EV_POWER_MANAGER_H

#include <stdint.h>
#include "ev/quiescence_service.h"
#include "ev/result.h"

struct ev_runtime_graph;

typedef struct {
    uint8_t timed_sleep_allowed;
    uint8_t one_way_sleep_allowed;
    uint8_t wake_gpio16_available;
    uint32_t min_sleep_ms;
    uint32_t max_sleep_ms;
    uint32_t required_wake_sources;
    uint32_t required_quiescence_mask;
    uint32_t log_flush_policy;
    uint32_t device_park_policy;
    uint32_t wake_reason_policy;
} ev_power_policy_t;

typedef struct {
    ev_power_policy_t policy;
    uint32_t sleep_accepted;
    uint32_t sleep_rejected;
} ev_power_manager_t;

void ev_power_manager_init(ev_power_manager_t *manager, const ev_power_policy_t *policy);
ev_result_t ev_power_manager_can_sleep_at(struct ev_runtime_graph *graph, ev_power_manager_t *manager, uint32_t now_ms, uint32_t requested_sleep_ms, const ev_quiescence_policy_t *quiescence_policy, ev_quiescence_report_t *out_report);
ev_result_t ev_power_manager_can_sleep(struct ev_runtime_graph *graph, ev_power_manager_t *manager, uint32_t requested_sleep_ms, ev_quiescence_report_t *out_report);

#endif
