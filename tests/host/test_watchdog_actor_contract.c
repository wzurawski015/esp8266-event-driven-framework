#include <assert.h>
#include <string.h>

#include "ev/msg.h"
#include "ev/watchdog_actor.h"
#include "fakes/fake_wdt_port.h"

static ev_watchdog_liveness_snapshot_t g_snapshot;
static ev_result_t g_liveness_result;
static unsigned g_liveness_calls;

static void reset_liveness(void)
{
    memset(&g_snapshot, 0, sizeof(g_snapshot));
    g_snapshot.system_turn_counter = 1U;
    g_snapshot.system_messages_processed = 1U;
    g_snapshot.domain_count = 1U;
    g_snapshot.domains[0].domain = EV_DOMAIN_FAST_LOOP;
    g_snapshot.domains[0].bound = true;
    g_snapshot.domains[0].last_result = EV_OK;
    g_liveness_result = EV_OK;
    g_liveness_calls = 0U;
}

static ev_result_t liveness_cb(void *ctx, ev_watchdog_liveness_snapshot_t *out_snapshot)
{
    (void)ctx;
    assert(out_snapshot != NULL);
    ++g_liveness_calls;
    *out_snapshot = g_snapshot;
    return g_liveness_result;
}

static void make_tick(ev_msg_t *msg)
{
    assert(ev_msg_init_publish(msg, EV_TICK_1S, ACT_APP) == EV_OK);
}

static void test_no_liveness_callback_does_not_feed(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, NULL, NULL) == EV_OK);
    assert(fake.enable_calls == 1U);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 0U);
    assert(ctx.stats.health_rejects == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_NO_LIVENESS);
}

static void test_unhealthy_liveness_does_not_feed(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    g_snapshot.permanent_stall = true;
    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 0U);
    assert(ctx.stats.health_rejects == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_PERMANENT_STALL);
}

static void test_healthy_liveness_feeds_once(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 1U);
    assert(ctx.stats.feeds_ok == 1U);
    assert(ctx.stats.feed_attempts == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_NONE);
}

static void test_stale_liveness_stops_feeding(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 1U);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 1U);
    assert(ctx.stats.health_rejects == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_NOT_PROGRESSING);
}


static void test_no_system_turn_does_not_feed(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    g_snapshot.system_turn_counter = 0U;
    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 0U);
    assert(ctx.stats.health_rejects == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_NO_SYSTEM_TURN);
}

static void test_sleep_arming_does_not_feed(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    g_snapshot.sleep_arming = true;
    fake_wdt_port_init(&fake);
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 0U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_SLEEP_ARMING);
}

static void test_feed_failure_is_counted(void)
{
    fake_wdt_port_t fake;
    ev_wdt_port_t port;
    ev_watchdog_actor_ctx_t ctx;
    ev_msg_t msg = EV_MSG_INITIALIZER;

    reset_liveness();
    fake_wdt_port_init(&fake);
    fake.next_feed_result = EV_ERR_STATE;
    fake_wdt_port_bind(&port, &fake);
    assert(ev_watchdog_actor_init(&ctx, &port, 3000U, liveness_cb, NULL) == EV_OK);
    make_tick(&msg);
    assert(ev_watchdog_actor_handle(&ctx, &msg) == EV_OK);
    assert(fake.feed_calls == 1U);
    assert(ctx.stats.feed_attempts == 1U);
    assert(ctx.stats.feeds_failed == 1U);
    assert(ctx.stats.last_reject_reason == EV_WATCHDOG_REJECT_FEED_FAILED);
}

int main(void)
{
    test_no_liveness_callback_does_not_feed();
    test_unhealthy_liveness_does_not_feed();
    test_healthy_liveness_feeds_once();
    test_stale_liveness_stops_feeding();
    test_no_system_turn_does_not_feed();
    test_sleep_arming_does_not_feed();
    test_feed_failure_is_counted();
    return 0;
}
