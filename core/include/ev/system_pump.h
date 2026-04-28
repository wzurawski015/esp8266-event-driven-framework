#ifndef EV_SYSTEM_PUMP_H
#define EV_SYSTEM_PUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/domain_pump.h"
#include "ev/result.h"

/**
 * @brief Cumulative counters owned by one multi-domain cooperative pump.
 */
typedef struct {
    uint32_t run_calls;
    uint32_t empty_calls;
    uint32_t budget_hits;
    uint32_t domains_pumped;
    size_t turns_processed;
    size_t messages_processed;
    size_t last_turn_budget;
    size_t last_turns_processed;
    size_t pending_high_watermark;
    size_t max_domains_examined_per_call;
    size_t max_domains_pumped_per_call;
    size_t max_turns_per_call;
    size_t max_messages_per_call;
    ev_execution_domain_t last_domain;
    ev_result_t last_result;
} ev_system_pump_stats_t;

/**
 * @brief Cooperative scheduler layer spanning multiple execution domains.
 *
 * Each bound domain receives one turn at a time in round-robin order. One turn
 * drains that domain using its default cooperative quantum derived from actor
 * SSOT drain budgets.
 */
typedef struct {
    ev_domain_pump_t *slots[EV_DOMAIN_COUNT];
    size_t next_domain_index;
    ev_system_pump_stats_t stats;
} ev_system_pump_t;

/**
 * @brief Report produced by one multi-domain cooperative run.
 */
typedef struct {
    size_t turn_budget;
    size_t turns_processed;
    size_t domains_examined;
    size_t domains_pumped;
    size_t messages_processed;
    size_t pending_before;
    size_t pending_after;
    bool exhausted_turn_budget;
    ev_execution_domain_t last_domain;
    ev_result_t stop_result;
} ev_system_pump_report_t;

/**
 * @brief Reset one system-pump report to a stable empty state.
 *
 * @param report Report to clear.
 */
void ev_system_pump_report_reset(ev_system_pump_report_t *report);

/**
 * @brief Initialize one empty system pump.
 *
 * @param pump Pump to initialize.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_system_pump_init(ev_system_pump_t *pump);

/**
 * @brief Bind one domain pump into the system pump.
 *
 * Only one bound domain pump may exist per execution domain.
 *
 * @param pump System pump to update.
 * @param domain_pump Domain pump to bind.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_system_pump_bind(ev_system_pump_t *pump, ev_domain_pump_t *domain_pump);

/**
 * @brief Reset cumulative counters for one system pump.
 *
 * Bound slots and scheduling cursor are preserved.
 *
 * @param pump Pump to update.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_system_pump_reset_stats(ev_system_pump_t *pump);

/**
 * @brief Return a stable pointer to system-pump counters.
 *
 * @param pump Pump to inspect.
 * @return Pointer to counters or NULL when pump is NULL.
 */
const ev_system_pump_stats_t *ev_system_pump_stats(const ev_system_pump_t *pump);

/**
 * @brief Return the number of currently bound execution domains.
 *
 * @param pump Pump to inspect.
 * @return Number of bound domain pumps.
 */
size_t ev_system_pump_bound_count(const ev_system_pump_t *pump);

/**
 * @brief Return the total pending message count across all bound domains.
 *
 * @param pump Pump to inspect.
 * @return Total pending message count.
 */
size_t ev_system_pump_pending(const ev_system_pump_t *pump);

/**
 * @brief Run up to @p turn_budget cooperative domain turns.
 *
 * One turn selects one bound domain and drains it using that domain's default
 * cooperative quantum from `ev_domain_pump_default_budget()`.
 *
 * @param pump Pump to run.
 * @param turn_budget Maximum number of domain turns to execute.
 * @param report Optional report receiving run details.
 * @return EV_OK on success, EV_ERR_EMPTY when no work was pending at entry, or
 *         an error code.
 */
ev_result_t ev_system_pump_run(
    ev_system_pump_t *pump,
    size_t turn_budget,
    ev_system_pump_report_t *report);

#endif /* EV_SYSTEM_PUMP_H */
