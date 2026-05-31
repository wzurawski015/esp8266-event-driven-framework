#include <assert.h>

#include "ev/power_state_machine.h"
#include "ev/power_actor.h"

static void test_success_path_visits_required_states(void)
{
    ev_power_state_machine_t sm;
    ev_power_transition_report_t report;
    ev_power_transition_input_t ok = { EV_OK, EV_POWER_SLEEP_REJECT_NONE };

    ev_power_state_machine_init(&sm);
    assert(sm.state == EV_POWER_STATE_ACTIVE);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_REQUEST_SLEEP, &ok, &report) == EV_OK);
    assert(report.previous_state == EV_POWER_STATE_ACTIVE);
    assert(report.new_state == EV_POWER_STATE_SLEEP_REQUESTED);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DRAIN_COMPLETE, &ok, &report) == EV_OK);
    assert(report.new_state == EV_POWER_STATE_DRAINING_RUNTIME);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_LOG_FLUSH_COMPLETE, &ok, &report) == EV_OK);
    assert(report.new_state == EV_POWER_STATE_LOG_FLUSHING);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_PORTS_PREPARED, &ok, &report) == EV_OK);
    assert(report.new_state == EV_POWER_STATE_PORTS_PREPARE_SLEEP);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_RTC_STATE_SAVED, &ok, &report) == EV_OK);
    assert(report.new_state == EV_POWER_STATE_RTC_STATE_SAVED);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DEEP_SLEEP_ENTERED, &ok, &report) == EV_OK);
    assert(report.new_state == EV_POWER_STATE_ENTERING_DEEP_SLEEP);
    assert(sm.transition_count == 6U);
}

static void test_reject_and_failed_are_explicit_terminal_states(void)
{
    ev_power_state_machine_t sm;
    ev_power_transition_input_t rejected = { EV_ERR_STATE, EV_POWER_SLEEP_REJECT_NOT_QUIESCENT };
    ev_power_transition_input_t failed = { EV_ERR_STATE, EV_POWER_SLEEP_REJECT_LOG_FLUSH_FAILED };

    ev_power_state_machine_init(&sm);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_REQUEST_SLEEP, NULL, NULL) == EV_OK);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DRAIN_REJECTED, &rejected, NULL) == EV_OK);
    assert(sm.state == EV_POWER_STATE_REJECTED);
    assert(sm.last_error == EV_ERR_STATE);
    assert(sm.last_reason == EV_POWER_SLEEP_REJECT_NOT_QUIESCENT);

    ev_power_state_machine_init(&sm);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_REQUEST_SLEEP, NULL, NULL) == EV_OK);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DRAIN_COMPLETE, NULL, NULL) == EV_OK);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_LOG_FLUSH_FAILED, &failed, NULL) == EV_OK);
    assert(sm.state == EV_POWER_STATE_FAILED);
    assert(sm.last_reason == EV_POWER_SLEEP_REJECT_LOG_FLUSH_FAILED);
}

static void test_invalid_transition_is_rejected_without_state_change(void)
{
    ev_power_state_machine_t sm;
    ev_power_state_machine_init(&sm);
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_DEEP_SLEEP_ENTERED, NULL, NULL) == EV_ERR_STATE);
    assert(sm.state == EV_POWER_STATE_ACTIVE);
    assert(sm.last_error == EV_ERR_STATE);
    assert(sm.transition_count == 0U);
}

static void test_wake_boot_can_return_to_active(void)
{
    ev_power_state_machine_t sm;
    ev_power_state_machine_init(&sm);
    sm.state = EV_POWER_STATE_WAKE_BOOT;
    assert(ev_power_state_machine_step(&sm, EV_POWER_ACTION_WAKE_BOOT, NULL, NULL) == EV_OK);
    assert(sm.state == EV_POWER_STATE_ACTIVE);
}

int main(void)
{
    test_success_path_visits_required_states();
    test_reject_and_failed_are_explicit_terminal_states();
    test_invalid_transition_is_rejected_without_state_change();
    test_wake_boot_can_return_to_active();
    return 0;
}
