#ifndef EV_EXECUTION_DOMAIN_H
#define EV_EXECUTION_DOMAIN_H

/**
 * @brief Logical execution domains used to map actors onto runtime contexts.
 */
typedef enum {
    EV_DOMAIN_ISR = 0,
    EV_DOMAIN_FAST_LOOP = 1,
    EV_DOMAIN_SLOW_IO = 2,
    EV_DOMAIN_NETWORK = 3,
    EV_DOMAIN_COUNT = 4
} ev_execution_domain_t;

/**
 * @brief Return a stable textual name for an execution domain.
 *
 * @param domain Execution domain enumerator.
 * @return Constant string describing the execution domain.
 */
const char *ev_execution_domain_name(ev_execution_domain_t domain);

#endif /* EV_EXECUTION_DOMAIN_H */
