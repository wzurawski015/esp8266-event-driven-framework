#ifndef EV_DOMAIN_PUMP_H
#define EV_DOMAIN_PUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/actor_runtime.h"
#include "ev/execution_domain.h"
#include "ev/result.h"

/**
 * @brief Cumulative counters owned by one domain pump.
 *
 * These counters describe scheduler-style cooperative draining over all actors
 * assigned to one logical execution domain.
 */
typedef struct {
    uint32_t pump_calls;
    uint32_t pump_empty_calls;
    uint32_t pump_budget_hits;
    size_t last_budget;
    size_t last_processed;
    size_t pending_high_watermark;
    size_t max_actors_examined_per_call;
    size_t max_actors_pumped_per_call;
    size_t max_messages_per_call;
    ev_actor_id_t last_actor;
    ev_result_t last_result;
} ev_domain_pump_stats_t;

/**
 * @brief Scheduler-style cooperative drain state for one execution domain.
 */
typedef struct {
    ev_actor_registry_t *registry;
    ev_execution_domain_t domain;
    size_t next_actor_index;
    ev_domain_pump_stats_t stats;
} ev_domain_pump_t;

/**
 * @brief Report produced by one cooperative domain drain.
 */
typedef struct {
    size_t budget;
    size_t processed;
    size_t pending_before;
    size_t pending_after;
    size_t actors_examined;
    size_t actors_pumped;
    bool exhausted_budget;
    ev_actor_id_t last_actor;
    ev_result_t stop_result;
} ev_domain_pump_report_t;

/**
 * @brief Reset one domain-pump report to a stable empty state.
 *
 * @param report Report to clear.
 */
void ev_domain_pump_report_reset(ev_domain_pump_report_t *report);

/**
 * @brief Initialize one cooperative domain pump.
 *
 * @param pump Pump state to initialize.
 * @param registry Actor registry inspected by this pump.
 * @param domain Execution domain drained by this pump.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_domain_pump_init(
    ev_domain_pump_t *pump,
    ev_actor_registry_t *registry,
    ev_execution_domain_t domain);

/**
 * @brief Reset cumulative counters for one domain pump.
 *
 * Scheduling cursor state is preserved.
 *
 * @param pump Pump to update.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_domain_pump_reset_stats(ev_domain_pump_t *pump);

/**
 * @brief Return a stable pointer to domain-pump counters.
 *
 * @param pump Pump to inspect.
 * @return Pointer to counters or NULL when pump is NULL.
 */
const ev_domain_pump_stats_t *ev_domain_pump_stats(const ev_domain_pump_t *pump);

/**
 * @brief Return the total number of pending messages in one domain.
 *
 * @param pump Pump to inspect.
 * @return Pending message count across all bound actors in the domain.
 */
size_t ev_domain_pump_pending(const ev_domain_pump_t *pump);

/**
 * @brief Return the default domain quantum derived from actor SSOT budgets.
 *
 * The value is the sum of `drain_budget` across all actors assigned to the
 * pump domain. It is used by higher scheduler layers as the default bounded
 * cooperative quantum for one full domain turn.
 *
 * @param pump Pump to inspect.
 * @return Default domain budget or 0 when pump is NULL or invalid.
 */
size_t ev_domain_pump_default_budget(const ev_domain_pump_t *pump);

/**
 * @brief Drain up to @p budget messages across all actors in one domain.
 *
 * The pump iterates actors in round-robin order, starting from the internal
 * cursor, and drains each selected actor with the smaller of its SSOT default
 * budget and the remaining domain budget.
 *
 * @param pump Pump state.
 * @param budget Maximum number of messages to process in total.
 * @param report Optional report receiving drain details.
 * @return EV_OK on success, EV_ERR_EMPTY when no work was pending, or an error code.
 */
ev_result_t ev_domain_pump_run(
    ev_domain_pump_t *pump,
    size_t budget,
    ev_domain_pump_report_t *report);

#endif /* EV_DOMAIN_PUMP_H */
