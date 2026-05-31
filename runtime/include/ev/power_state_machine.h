#ifndef EV_POWER_STATE_MACHINE_H
#define EV_POWER_STATE_MACHINE_H

#include <stdint.h>
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ev_power_state {
    EV_POWER_STATE_ACTIVE = 0,
    EV_POWER_STATE_SLEEP_REQUESTED = 1,
    EV_POWER_STATE_DRAINING_RUNTIME = 2,
    EV_POWER_STATE_LOG_FLUSHING = 3,
    EV_POWER_STATE_PORTS_PREPARE_SLEEP = 4,
    EV_POWER_STATE_RTC_STATE_SAVED = 5,
    EV_POWER_STATE_ENTERING_DEEP_SLEEP = 6,
    EV_POWER_STATE_WAKE_BOOT = 7,
    EV_POWER_STATE_REJECTED = 8,
    EV_POWER_STATE_FAILED = 9
} ev_power_state_t;

typedef enum ev_power_action {
    EV_POWER_ACTION_REQUEST_SLEEP = 0,
    EV_POWER_ACTION_DRAIN_COMPLETE = 1,
    EV_POWER_ACTION_DRAIN_REJECTED = 2,
    EV_POWER_ACTION_LOG_FLUSH_COMPLETE = 3,
    EV_POWER_ACTION_LOG_FLUSH_FAILED = 4,
    EV_POWER_ACTION_PORTS_PREPARED = 5,
    EV_POWER_ACTION_PORTS_PREPARE_FAILED = 6,
    EV_POWER_ACTION_RTC_STATE_SAVED = 7,
    EV_POWER_ACTION_RTC_STATE_SAVE_FAILED = 8,
    EV_POWER_ACTION_DEEP_SLEEP_ENTERED = 9,
    EV_POWER_ACTION_DEEP_SLEEP_FAILED = 10,
    EV_POWER_ACTION_WAKE_BOOT = 11
} ev_power_action_t;

typedef struct ev_power_transition_input {
    ev_result_t result;
    uint32_t reason;
} ev_power_transition_input_t;

typedef struct ev_power_transition_report {
    ev_power_state_t previous_state;
    ev_power_state_t new_state;
    ev_power_action_t action;
    ev_result_t result;
    uint32_t reason;
} ev_power_transition_report_t;

typedef struct ev_power_state_machine {
    ev_power_state_t state;
    ev_power_state_t previous_state;
    ev_power_action_t last_action;
    ev_result_t last_error;
    uint32_t last_reason;
    uint32_t transition_count;
} ev_power_state_machine_t;

void ev_power_state_machine_init(ev_power_state_machine_t *sm);
ev_result_t ev_power_state_machine_step(ev_power_state_machine_t *sm,
                                        ev_power_action_t action,
                                        const ev_power_transition_input_t *input,
                                        ev_power_transition_report_t *out_report);
const char *ev_power_state_name(ev_power_state_t state);
const char *ev_power_action_name(ev_power_action_t action);

#ifdef __cplusplus
}
#endif

#endif /* EV_POWER_STATE_MACHINE_H */
