#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/demo_runtime_instances.h"
#include "fakes/fake_log_port.h"

typedef struct { ev_time_mono_us_t now_us; } test_clock_t;
static ev_result_t now_fn(void *ctx, ev_time_mono_us_t *out_now) { test_clock_t *c=(test_clock_t*)ctx; *out_now=c->now_us; return EV_OK; }
static ev_result_t wall_fn(void *ctx, ev_time_wall_us_t *out_now) { (void)ctx; (void)out_now; return EV_ERR_UNSUPPORTED; }
static ev_result_t delay_fn(void *ctx, uint32_t ms) { test_clock_t *c=(test_clock_t*)ctx; c->now_us += (ev_time_mono_us_t)ms * 1000ULL; return EV_OK; }

int main(void)
{
    test_clock_t clock = {0};
    fake_log_port_t fake_log;
    ev_clock_port_t clock_port = { .ctx=&clock, .mono_now_us=now_fn, .wall_now_us=wall_fn, .delay_ms=delay_fn };
    ev_log_port_t log_port = {0};
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;
    ev_actor_instance_descriptor_t instances[EV_ACTOR_COUNT];
    size_t count = 0U;
    size_t i;
    uint32_t seen_mask = 0U;

    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);
    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "instances";
    cfg.board_name = "host";
    cfg.tick_period_ms = 1000U;
    cfg.clock_port = &clock_port;
    cfg.log_port = &log_port;
    cfg.board_profile = ev_demo_app_default_board_profile();
    assert(ev_demo_app_init(&app, &cfg) == EV_OK);
    assert(ev_demo_runtime_instances_init(&app, instances, EV_ACTOR_COUNT, &count) == EV_OK);
    assert(count == ev_demo_runtime_instance_count(&app));
    assert(count >= 7U);
    for (i = 0U; i < count; ++i) {
        assert(instances[i].actor_id < EV_ACTOR_COUNT);
        assert(instances[i].actor_context != NULL);
        assert(instances[i].handler_fn != NULL);
        assert((seen_mask & (1UL << instances[i].actor_id)) == 0U);
        seen_mask |= (1UL << instances[i].actor_id);
    }
    assert((seen_mask & (1UL << ACT_APP)) != 0U);
    assert((seen_mask & (1UL << ACT_RUNTIME)) != 0U);
    assert((seen_mask & (1UL << ACT_POWER)) != 0U);
    assert((seen_mask & (1UL << ACT_NETWORK)) == 0U);
    assert((seen_mask & (1UL << ACT_WATCHDOG)) == 0U);
    return 0;
}
