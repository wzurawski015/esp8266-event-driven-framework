#include "ev/power_state_machine.h"

#include <string.h>

void ev_power_state_machine_init(ev_power_state_machine_t *sm)
{
    if (sm != NULL) {
        (void)memset(sm, 0, sizeof(*sm));
        sm->state = EV_POWER_STATE_ACTIVE;
        sm->previous_state = EV_POWER_STATE_ACTIVE;
        sm->last_error = EV_OK;
    }
}

const char *ev_power_state_name(ev_power_state_t state)
{
    switch (state) {
    case EV_POWER_STATE_ACTIVE: return "ACTIVE";
    case EV_POWER_STATE_SLEEP_REQUESTED: return "SLEEP_REQUESTED";
    case EV_POWER_STATE_DRAINING_RUNTIME: return "DRAINING_RUNTIME";
    case EV_POWER_STATE_LOG_FLUSHING: return "LOG_FLUSHING";
    case EV_POWER_STATE_PORTS_PREPARE_SLEEP: return "PORTS_PREPARE_SLEEP";
    case EV_POWER_STATE_RTC_STATE_SAVED: return "RTC_STATE_SAVED";
    case EV_POWER_STATE_ENTERING_DEEP_SLEEP: return "ENTERING_DEEP_SLEEP";
    case EV_POWER_STATE_WAKE_BOOT: return "WAKE_BOOT";
    case EV_POWER_STATE_REJECTED: return "REJECTED";
    case EV_POWER_STATE_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}

const char *ev_power_action_name(ev_power_action_t action)
{
    switch (action) {
    case EV_POWER_ACTION_REQUEST_SLEEP: return "REQUEST_SLEEP";
    case EV_POWER_ACTION_DRAIN_COMPLETE: return "DRAIN_COMPLETE";
    case EV_POWER_ACTION_DRAIN_REJECTED: return "DRAIN_REJECTED";
    case EV_POWER_ACTION_LOG_FLUSH_COMPLETE: return "LOG_FLUSH_COMPLETE";
    case EV_POWER_ACTION_LOG_FLUSH_FAILED: return "LOG_FLUSH_FAILED";
    case EV_POWER_ACTION_PORTS_PREPARED: return "PORTS_PREPARED";
    case EV_POWER_ACTION_PORTS_PREPARE_FAILED: return "PORTS_PREPARE_FAILED";
    case EV_POWER_ACTION_RTC_STATE_SAVED: return "RTC_STATE_SAVED";
    case EV_POWER_ACTION_RTC_STATE_SAVE_FAILED: return "RTC_STATE_SAVE_FAILED";
    case EV_POWER_ACTION_DEEP_SLEEP_ENTERED: return "DEEP_SLEEP_ENTERED";
    case EV_POWER_ACTION_DEEP_SLEEP_FAILED: return "DEEP_SLEEP_FAILED";
    case EV_POWER_ACTION_WAKE_BOOT: return "WAKE_BOOT";
    default: return "UNKNOWN";
    }
}

static int ev_power_transition_allowed(ev_power_state_t from, ev_power_action_t action, ev_power_state_t *to)
{
    if (to == NULL) {
        return 0;
    }
    switch (action) {
    case EV_POWER_ACTION_REQUEST_SLEEP:
        if (from == EV_POWER_STATE_ACTIVE) { *to = EV_POWER_STATE_SLEEP_REQUESTED; return 1; }
        break;
    case EV_POWER_ACTION_DRAIN_COMPLETE:
        if (from == EV_POWER_STATE_SLEEP_REQUESTED) { *to = EV_POWER_STATE_DRAINING_RUNTIME; return 1; }
        break;
    case EV_POWER_ACTION_DRAIN_REJECTED:
        if ((from == EV_POWER_STATE_SLEEP_REQUESTED) || (from == EV_POWER_STATE_DRAINING_RUNTIME)) { *to = EV_POWER_STATE_REJECTED; return 1; }
        break;
    case EV_POWER_ACTION_LOG_FLUSH_COMPLETE:
        if (from == EV_POWER_STATE_DRAINING_RUNTIME) { *to = EV_POWER_STATE_LOG_FLUSHING; return 1; }
        break;
    case EV_POWER_ACTION_LOG_FLUSH_FAILED:
        if ((from == EV_POWER_STATE_DRAINING_RUNTIME) || (from == EV_POWER_STATE_LOG_FLUSHING)) { *to = EV_POWER_STATE_FAILED; return 1; }
        break;
    case EV_POWER_ACTION_PORTS_PREPARED:
        if (from == EV_POWER_STATE_LOG_FLUSHING) { *to = EV_POWER_STATE_PORTS_PREPARE_SLEEP; return 1; }
        break;
    case EV_POWER_ACTION_PORTS_PREPARE_FAILED:
        if ((from == EV_POWER_STATE_LOG_FLUSHING) || (from == EV_POWER_STATE_PORTS_PREPARE_SLEEP)) { *to = EV_POWER_STATE_FAILED; return 1; }
        break;
    case EV_POWER_ACTION_RTC_STATE_SAVED:
        if (from == EV_POWER_STATE_PORTS_PREPARE_SLEEP) { *to = EV_POWER_STATE_RTC_STATE_SAVED; return 1; }
        break;
    case EV_POWER_ACTION_RTC_STATE_SAVE_FAILED:
        if (from == EV_POWER_STATE_PORTS_PREPARE_SLEEP) { *to = EV_POWER_STATE_FAILED; return 1; }
        break;
    case EV_POWER_ACTION_DEEP_SLEEP_ENTERED:
        if (from == EV_POWER_STATE_RTC_STATE_SAVED) { *to = EV_POWER_STATE_ENTERING_DEEP_SLEEP; return 1; }
        break;
    case EV_POWER_ACTION_DEEP_SLEEP_FAILED:
        if ((from == EV_POWER_STATE_RTC_STATE_SAVED) || (from == EV_POWER_STATE_ENTERING_DEEP_SLEEP)) { *to = EV_POWER_STATE_FAILED; return 1; }
        break;
    case EV_POWER_ACTION_WAKE_BOOT:
        if ((from == EV_POWER_STATE_WAKE_BOOT) || (from == EV_POWER_STATE_ENTERING_DEEP_SLEEP) || (from == EV_POWER_STATE_FAILED) || (from == EV_POWER_STATE_REJECTED)) { *to = EV_POWER_STATE_ACTIVE; return 1; }
        break;
    default:
        break;
    }
    return 0;
}

ev_result_t ev_power_state_machine_step(ev_power_state_machine_t *sm,
                                        ev_power_action_t action,
                                        const ev_power_transition_input_t *input,
                                        ev_power_transition_report_t *out_report)
{
    ev_power_state_t next_state = EV_POWER_STATE_FAILED;
    ev_result_t input_result = (input != NULL) ? input->result : EV_OK;
    uint32_t input_reason = (input != NULL) ? input->reason : 0U;
    ev_power_state_t previous;

    if (sm == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    previous = sm->state;
    if (ev_power_transition_allowed(previous, action, &next_state) == 0) {
        sm->previous_state = previous;
        sm->last_action = action;
        sm->last_error = EV_ERR_STATE;
        sm->last_reason = input_reason;
        if (out_report != NULL) {
            out_report->previous_state = previous;
            out_report->new_state = sm->state;
            out_report->action = action;
            out_report->result = EV_ERR_STATE;
            out_report->reason = input_reason;
        }
        return EV_ERR_STATE;
    }
    sm->previous_state = previous;
    sm->state = next_state;
    sm->last_action = action;
    sm->last_error = input_result;
    sm->last_reason = input_reason;
    ++sm->transition_count;
    if (out_report != NULL) {
        out_report->previous_state = previous;
        out_report->new_state = next_state;
        out_report->action = action;
        out_report->result = input_result;
        out_report->reason = input_reason;
    }
    return EV_OK;
}
