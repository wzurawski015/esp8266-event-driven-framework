#include <assert.h>
#include <stdint.h>

#include "ev/dispose.h"
#include "ev/power_actor.h"
#include "fakes/fake_log_port.h"
#include "fakes/fake_system_port.h"

typedef struct {
    ev_result_t result;
    uint32_t calls;
    uint32_t order;
    uint32_t *shared_sequence;
    ev_power_quiescence_report_t report;
} fake_guard_t;

typedef struct {
    ev_result_t arm_result;
    ev_result_t disarm_result;
    uint32_t arm_calls;
    uint32_t disarm_calls;
    uint32_t arm_order;
    uint32_t disarm_order;
    uint32_t *shared_sequence;
    ev_power_quiescence_report_t report;
} fake_arming_t;

static ev_msg_t make_sleep_cmd(uint32_t duration_ms)
{
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_sys_goto_sleep_cmd_t cmd = {0};

    cmd.duration_ms = duration_ms;
    assert(ev_msg_init_publish(&msg, EV_SYS_GOTO_SLEEP_CMD, ACT_APP) == EV_OK);
    assert(ev_msg_set_inline_payload(&msg, &cmd, sizeof(cmd)) == EV_OK);
    return msg;
}

static ev_result_t fake_guard(void *ctx, uint64_t duration_us, ev_power_quiescence_report_t *out_report)
{
    fake_guard_t *guard = (fake_guard_t *)ctx;

    assert(guard != NULL);
    assert(duration_us > 0ULL);
    ++guard->calls;
    if (guard->shared_sequence != NULL) {
        ++(*guard->shared_sequence);
        guard->order = *guard->shared_sequence;
    }
    if (out_report != NULL) {
        *out_report = guard->report;
    }
    return guard->result;
}

static ev_result_t fake_sleep_arm(void *ctx, uint64_t duration_us, ev_power_quiescence_report_t *out_report)
{
    fake_arming_t *arming = (fake_arming_t *)ctx;

    assert(arming != NULL);
    assert(duration_us > 0ULL);
    ++arming->arm_calls;
    if (arming->shared_sequence != NULL) {
        ++(*arming->shared_sequence);
        arming->arm_order = *arming->shared_sequence;
    }
    if (out_report != NULL) {
        *out_report = arming->report;
    }
    return arming->arm_result;
}

static ev_result_t fake_sleep_disarm(void *ctx)
{
    fake_arming_t *arming = (fake_arming_t *)ctx;

    assert(arming != NULL);
    ++arming->disarm_calls;
    if (arming->shared_sequence != NULL) {
        ++(*arming->shared_sequence);
        arming->disarm_order = *arming->shared_sequence;
    }
    return arming->disarm_result;
}

static void test_successful_sleep_order_and_counters(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    fake_guard_t guard = {0};
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(1234U);
    uint32_t sequence = 0U;

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system.external_sequence = &sequence;
    fake_log.shared_sequence = &sequence;
    guard.shared_sequence = &sequence;
    guard.result = EV_OK;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_set_quiescence_guard(&power, fake_guard, &guard) == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_OK);

    assert(guard.calls == 1U);
    assert(fake_log.write_calls == 1U);
    assert(fake_log.flush_calls == 1U);
    assert(fake_system.prepare_for_sleep_calls == 1U);
    assert(fake_system.deep_sleep_calls == 1U);
    assert(guard.order < fake_log.write_order);
    assert(fake_log.write_order < fake_log.flush_order);
    assert(fake_log.flush_order < fake_system.prepare_order);
    assert(fake_system.prepare_order < fake_system.deep_sleep_order);
    assert(fake_system.last_prepare_duration_us == 1234000ULL);
    assert(fake_system.last_duration_us == 1234000ULL);
    assert(power.sleep_requests_seen == 1U);
    assert(power.sleep_requests_accepted == 1U);
    assert(power.sleep_requests_rejected == 0U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_NONE);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_quiescence_guard_rejects_before_prepare(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    fake_guard_t guard = {0};
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    guard.result = EV_ERR_STATE;
    guard.report.pending_actor_messages = 1U;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_set_quiescence_guard(&power, fake_guard, &guard) == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(guard.calls == 1U);
    assert(fake_log.write_calls == 0U);
    assert(fake_log.flush_calls == 0U);
    assert(fake_system.prepare_for_sleep_calls == 0U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.sleep_requests_seen == 1U);
    assert(power.sleep_requests_accepted == 0U);
    assert(power.sleep_requests_rejected == 1U);
    assert(power.sleep_quiescence_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_NOT_QUIESCENT);
    assert(power.last_quiescence_report.pending_actor_messages == 1U);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_log_flush_failure_rejects_before_prepare(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_log.next_flush_result = EV_ERR_STATE;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(fake_log.write_calls == 1U);
    assert(fake_log.flush_calls == 1U);
    assert(fake_system.prepare_for_sleep_calls == 0U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.sleep_requests_rejected == 1U);
    assert(power.log_flush_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_LOG_FLUSH_FAILED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_prepare_failure_rejects_sleep(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system.next_prepare_result = EV_ERR_STATE;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(fake_system.prepare_for_sleep_calls == 1U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.sleep_requests_seen == 1U);
    assert(power.sleep_requests_accepted == 0U);
    assert(power.sleep_requests_rejected == 1U);
    assert(power.prepare_for_sleep_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_PREPARE_FAILED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_deep_sleep_failure_is_counted_after_accept(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system.next_result = EV_ERR_STATE;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(fake_system.prepare_for_sleep_calls == 1U);
    assert(fake_system.deep_sleep_calls == 1U);
    assert(power.sleep_requests_accepted == 1U);
    assert(power.sleep_requests_rejected == 0U);
    assert(power.deep_sleep_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_DEEP_SLEEP_FAILED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_bad_duration_rejected_before_port_calls(void)
{
    fake_system_port_t fake_system;
    ev_system_port_t system_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(0U);

    fake_system_port_init(&fake_system);
    fake_system_port_bind(&system_port, &fake_system);

    assert(ev_power_actor_init(&power, &system_port, NULL, "test_power") == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_OUT_OF_RANGE);
    assert(fake_system.prepare_for_sleep_calls == 0U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.sleep_requests_seen == 0U);
    assert(power.sleep_requests_rejected == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_BAD_DURATION);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_unsupported_port_is_counted_without_sleep(void)
{
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(1U);

    assert(ev_power_actor_init(&power, NULL, NULL, "test_power") == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_OK);
    assert(power.sleep_requests_seen == 1U);
    assert(power.sleep_requests_unsupported == 1U);
    assert(power.sleep_requests_accepted == 0U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_UNSUPPORTED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_sleep_arming_rejects_before_log_and_prepare(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    fake_arming_t arming = {0};
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);
    arming.arm_result = EV_ERR_STATE;
    arming.report.reason = EV_POWER_SLEEP_REJECT_NOT_QUIESCENT;
    arming.report.pending_irq_samples = 1U;

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_set_sleep_arming(&power, fake_sleep_arm, fake_sleep_disarm, &arming) == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(arming.arm_calls == 1U);
    assert(arming.disarm_calls == 0U);
    assert(fake_log.write_calls == 0U);
    assert(fake_log.flush_calls == 0U);
    assert(fake_system.prepare_for_sleep_calls == 0U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.sleep_arming_failures == 1U);
    assert(power.sleep_quiescence_failures == 1U);
    assert(power.sleep_requests_rejected == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_NOT_QUIESCENT);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_prepare_failure_disarms_sleep_arming(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    fake_arming_t arming = {0};
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system.next_prepare_result = EV_ERR_STATE;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);
    arming.arm_result = EV_OK;

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_set_sleep_arming(&power, fake_sleep_arm, fake_sleep_disarm, &arming) == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(arming.arm_calls == 1U);
    assert(arming.disarm_calls == 1U);
    assert(fake_system.prepare_for_sleep_calls == 1U);
    assert(fake_system.deep_sleep_calls == 0U);
    assert(power.prepare_for_sleep_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_PREPARE_FAILED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

static void test_deep_sleep_failure_cancels_platform_and_disarms(void)
{
    fake_system_port_t fake_system;
    fake_log_port_t fake_log;
    fake_arming_t arming = {0};
    ev_system_port_t system_port = {0};
    ev_log_port_t log_port = {0};
    ev_power_actor_ctx_t power = {0};
    ev_msg_t msg = make_sleep_cmd(10U);
    uint32_t sequence = 0U;

    fake_system_port_init(&fake_system);
    fake_log_port_init(&fake_log);
    fake_system.external_sequence = &sequence;
    fake_log.shared_sequence = &sequence;
    arming.shared_sequence = &sequence;
    fake_system.next_result = EV_ERR_STATE;
    fake_system_port_bind(&system_port, &fake_system);
    fake_log_port_bind(&log_port, &fake_log);
    arming.arm_result = EV_OK;

    assert(ev_power_actor_init(&power, &system_port, &log_port, "test_power") == EV_OK);
    assert(ev_power_actor_set_sleep_arming(&power, fake_sleep_arm, fake_sleep_disarm, &arming) == EV_OK);
    assert(ev_power_actor_handle(&power, &msg) == EV_ERR_STATE);
    assert(arming.arm_calls == 1U);
    assert(fake_system.prepare_for_sleep_calls == 1U);
    assert(fake_system.deep_sleep_calls == 1U);
    assert(fake_system.cancel_sleep_prepare_calls == 1U);
    assert(arming.disarm_calls == 1U);
    assert(arming.arm_order < fake_log.write_order);
    assert(fake_system.deep_sleep_order < fake_system.cancel_order);
    assert(fake_system.cancel_order < arming.disarm_order);
    assert(power.sleep_requests_accepted == 1U);
    assert(power.deep_sleep_failures == 1U);
    assert(power.last_reject_reason == EV_POWER_SLEEP_REJECT_DEEP_SLEEP_FAILED);

    assert(ev_msg_dispose(&msg) == EV_OK);
}

int main(void)
{
    test_successful_sleep_order_and_counters();
    test_quiescence_guard_rejects_before_prepare();
    test_log_flush_failure_rejects_before_prepare();
    test_prepare_failure_rejects_sleep();
    test_deep_sleep_failure_is_counted_after_accept();
    test_bad_duration_rejected_before_port_calls();
    test_unsupported_port_is_counted_without_sleep();
    test_sleep_arming_rejects_before_log_and_prepare();
    test_prepare_failure_disarms_sleep_arming();
    test_deep_sleep_failure_cancels_platform_and_disarms();
    return 0;
}
