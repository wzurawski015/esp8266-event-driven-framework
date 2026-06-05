#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/dispose.h"
#include "ev/ds18b20_actor.h"
#include "ev/ds18b20_driver.h"
#include "ev/msg.h"
#include "fakes/fake_onewire_port.h"

typedef struct {
    uint32_t ready_count;
    uint32_t temp_count;
    int16_t last_temp;
} capture_delivery_t;

static ev_result_t capture_delivery(ev_actor_id_t target_actor, const ev_msg_t *msg, void *context)
{
    capture_delivery_t *capture = (capture_delivery_t *)context;
    (void)target_actor;

    if ((capture == NULL) || (msg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (msg->event_id == EV_DS18B20_READY) {
        ++capture->ready_count;
        return EV_OK;
    }
    if (msg->event_id == EV_TEMP_UPDATED) {
        const ev_temp_payload_t *payload = (const ev_temp_payload_t *)ev_msg_payload_data(msg);
        assert(payload != NULL);
        ++capture->temp_count;
        capture->last_temp = payload->centi_celsius;
        return EV_OK;
    }
    return EV_ERR_CONTRACT;
}

static void seed_valid_scratchpad(uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES], int16_t raw)
{
    memset(scratchpad, 0, EV_DS18B20_SCRATCHPAD_BYTES);
    scratchpad[0] = (uint8_t)((uint16_t)raw & 0xFFU);
    scratchpad[1] = (uint8_t)(((uint16_t)raw >> 8U) & 0xFFU);
    scratchpad[2] = 0x4BU;
    scratchpad[3] = 0x46U;
    scratchpad[4] = 0x7FU;
    scratchpad[5] = 0xFFU;
    scratchpad[6] = 0x0CU;
    scratchpad[7] = 0x10U;
    scratchpad[8] = ev_ds18b20_crc8(scratchpad, EV_DS18B20_SCRATCHPAD_BYTES - 1U);
}

static void send_event(ev_ds18b20_actor_ctx_t *ctx, ev_event_id_t event_id)
{
    ev_msg_t msg = {0};
    assert(ev_msg_init_publish(&msg, event_id, ACT_APP) == EV_OK);
    assert(ev_ds18b20_actor_handle(ctx, &msg) == EV_OK);
    assert(ev_msg_dispose(&msg) == EV_OK);
}

int main(void)
{
    fake_onewire_port_t fake;
    ev_onewire_port_t port;
    ev_ds18b20_actor_ctx_t actor;
    capture_delivery_t capture = {0};
    uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES];
    uint32_t i;

    fake_onewire_port_init(&fake);
    fake_onewire_port_bind(&port, &fake);
    assert(ev_ds18b20_actor_init(&actor, &port, capture_delivery, &capture) == EV_OK);
    assert(actor.resolution_bits == 12U);
    assert(actor.conversion_wait_ms == 750U);
    assert(ev_ds18b20_actor_configure_resolution(&actor, 11U) == EV_OK);
    assert(actor.conversion_wait_ms == 375U);
    assert(ev_ds18b20_actor_configure_resolution(&actor, 8U) == EV_ERR_OUT_OF_RANGE);
    assert(ev_ds18b20_actor_configure_resolution(&actor, 12U) == EV_OK);

    send_event(&actor, EV_BOOT_COMPLETED);
    assert(actor.conversion_pending);
    assert(actor.conversion_started_at_ms == 0U);
    assert(actor.conversion_deadline_ms == 750U);
    assert(fake.write_calls == 2U);
    assert(ev_ds18b20_actor_configure_resolution(&actor, 10U) == EV_ERR_STATE);

    seed_valid_scratchpad(scratchpad, 0x0191);
    fake_onewire_port_seed_read_bytes(&fake, scratchpad, sizeof(scratchpad));
    for (i = 0U; i < 7U; ++i) {
        send_event(&actor, EV_TICK_100MS);
    }
    assert(actor.actor_now_ms == 700U);
    assert(fake.read_calls == 0U);
    assert(capture.temp_count == 0U);
    assert(actor.conversion_deadline_skips == 7U);

    send_event(&actor, EV_TICK_100MS);
    assert(actor.actor_now_ms == 800U);
    assert(fake.read_calls == EV_DS18B20_SCRATCHPAD_BYTES);
    assert(actor.scratchpad_reads_ok == 1U);
    assert(capture.ready_count == 1U);
    assert(capture.temp_count == 2U);
    assert(capture.last_temp == 2506);
    assert(actor.conversion_pending);
    assert(actor.conversion_deadline_ms == 1550U);

    seed_valid_scratchpad(scratchpad, (int16_t)0xFF5EU);
    fake_onewire_port_seed_read_bytes(&fake, scratchpad, sizeof(scratchpad));
    send_event(&actor, EV_TICK_1S);
    assert(capture.temp_count == 4U);
    assert(capture.last_temp < 0);

    seed_valid_scratchpad(scratchpad, 0x0191);
    scratchpad[0] ^= 0x01U;
    fake_onewire_port_seed_read_bytes(&fake, scratchpad, sizeof(scratchpad));
    send_event(&actor, EV_TICK_1S);
    assert(actor.crc_failures == 1U);
    assert(capture.temp_count == 4U);

    return 0;
}
