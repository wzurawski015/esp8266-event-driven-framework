#include <assert.h>
#include <stdint.h>

#include "ev/bh1750_actor.h"
#include "ev/dispose.h"
#include "ev/msg.h"
#include "fakes/fake_i2c_port.h"

typedef struct {
    uint32_t ready_count;
    uint32_t light_count;
    uint32_t last_milli_lux;
    uint16_t last_raw;
} capture_delivery_t;

static ev_result_t capture_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    capture_delivery_t *capture = (capture_delivery_t *)context;
    (void)target_actor;

    if ((capture == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (msg->event_id == EV_BH1750_READY) {
        ++capture->ready_count;
        return EV_OK;
    }
    if (msg->event_id == EV_LIGHT_UPDATED) {
        const ev_light_payload_t *payload = (const ev_light_payload_t *)ev_msg_payload_data(msg);
        assert(payload != NULL);
        assert(ev_msg_payload_size(msg) == sizeof(*payload));
        ++capture->light_count;
        capture->last_milli_lux = payload->milli_lux;
        capture->last_raw = payload->raw;
        return EV_OK;
    }
    return EV_ERR_CONTRACT;
}

static void send_event(ev_bh1750_actor_ctx_t *ctx, ev_event_id_t event_id)
{
    ev_msg_t msg = {0};
    assert(ev_msg_init_publish(&msg, event_id, ACT_APP) == EV_OK);
    assert(ev_bh1750_actor_handle(ctx, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

int main(void)
{
    fake_i2c_port_t fake;
    ev_i2c_port_t port;
    ev_bh1750_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    uint8_t raw_light[2] = {0x01U, 0x2CU};
    uint32_t i;

    fake_i2c_port_init(&fake);
    fake_i2c_port_bind(&port, &fake);
    fake_i2c_port_set_present(&fake, EV_BH1750_ADDR_LOW_7BIT, true);

    assert(ev_bh1750_actor_init(&actor,
                                &port,
                                EV_I2C_PORT_NUM_0,
                                EV_BH1750_ADDR_LOW_7BIT,
                                capture_delivery,
                                &capture) == EV_OK);
    assert(actor.mode == EV_BH1750_DEFAULT_MODE);
    assert(actor.measurement_wait_ms == 180U);
    assert(ev_bh1750_actor_configure_mode(&actor, EV_BH1750_MODE_ONE_TIME_LOW_RES) == EV_OK);
    assert(actor.measurement_wait_ms == 24U);
    assert(ev_bh1750_actor_configure_mode(&actor, (ev_bh1750_mode_t)0xFFU) == EV_ERR_INVALID_ARG);
    assert(ev_bh1750_actor_configure_mode(&actor, EV_BH1750_DEFAULT_MODE) == EV_OK);

    send_event(&actor, EV_BOOT_COMPLETED);
    assert(actor.measurement_pending);
    assert(actor.measurement_deadline_ms == 180U);
    assert(fake.write_stream_calls == 2U); /* power-on + measurement command */
    assert(ev_bh1750_actor_configure_mode(&actor, EV_BH1750_MODE_ONE_TIME_LOW_RES) == EV_ERR_STATE);

    fake_i2c_port_seed_read_stream(&fake, EV_BH1750_ADDR_LOW_7BIT, raw_light, sizeof(raw_light));
    send_event(&actor, EV_TICK_100MS);
    assert(actor.actor_now_ms == 100U);
    assert(actor.deadline_skips == 1U);
    assert(fake.read_stream_calls == 0U);
    assert(capture.light_count == 0U);

    {
        const uint32_t now_before = actor.actor_now_ms;
        send_event(&actor, EV_TICK_1S);
        assert(actor.actor_now_ms == now_before);
        assert(actor.noncanonical_ticks_ignored == 1U);
        assert(fake.read_stream_calls == 0U);
    }

    send_event(&actor, EV_TICK_100MS);
    assert(actor.actor_now_ms == 200U);
    assert(fake.read_stream_calls == 1U);
    assert(capture.ready_count == 1U);
    assert(capture.light_count == 2U);
    assert(capture.last_raw == 300U);
    assert(capture.last_milli_lux == ev_bh1750_raw_to_milli_lux(300U));
    assert(actor.measurement_pending);

    fake_i2c_port_set_status(&fake, EV_BH1750_ADDR_LOW_7BIT, EV_I2C_ERR_NACK);
    for (i = 0U; i < 2U; ++i) {
        send_event(&actor, EV_TICK_100MS);
    }
    assert(actor.no_device_failures == 1U);
    assert(actor.retry_backoff_ms == 1000U);
    assert(actor.retry_deadline_ms == actor.actor_now_ms + 1000U);
    assert(actor.optional_retry_backoffs == 1U);
    assert(!actor.measurement_pending);
    assert(capture.light_count == 2U);
    assert(actor.light_valid);

    {
        const uint32_t write_calls_before_backoff = fake.write_stream_calls;
        const uint32_t read_calls_before_backoff = fake.read_stream_calls;
        const uint32_t deadline = actor.retry_deadline_ms;
        while ((int32_t)(actor.actor_now_ms - deadline) < 0) {
            send_event(&actor, EV_TICK_100MS);
        }
        assert(actor.optional_retry_skips > 0U);
        assert(fake.read_stream_calls == read_calls_before_backoff);
        assert(fake.write_stream_calls == (write_calls_before_backoff + 1U));
        assert(actor.no_device_failures == 2U);
        assert(actor.retry_backoff_ms == 2000U);
        assert(!actor.measurement_pending);
    }

    fake_i2c_port_set_status(&fake, EV_BH1750_ADDR_LOW_7BIT, EV_I2C_OK);
    while (!actor.measurement_pending) {
        send_event(&actor, EV_TICK_100MS);
    }
    assert(actor.retry_backoff_ms == 0U);
    assert(actor.retry_deadline_ms == 0U);

    return 0;
}
