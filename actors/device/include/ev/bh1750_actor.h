#ifndef EV_BH1750_ACTOR_H
#define EV_BH1750_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/bh1750_driver.h"
#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/port_i2c.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_BH1750_DEFAULT_MODE EV_BH1750_MODE_ONE_TIME_HIGH_RES

typedef struct {
    uint32_t milli_lux;
    uint16_t raw;
} ev_light_payload_t;

EV_STATIC_ASSERT(sizeof(ev_light_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "BH1750 light payload must fit into one inline event payload");

typedef struct {
    ev_i2c_port_t *i2c_port;
    ev_delivery_fn_t deliver;
    void *deliver_context;
    ev_i2c_port_num_t port_num;
    uint8_t addr7;
    ev_bh1750_mode_t mode;
    uint16_t measurement_wait_ms;
    uint32_t actor_now_ms;
    uint32_t measurement_deadline_ms;
    uint32_t retry_deadline_ms;
    uint32_t retry_backoff_ms;
    bool measurement_pending;
    bool sensor_present;
    bool ready_published;
    bool last_read_ok;
    bool light_valid;
    uint16_t last_raw;
    uint32_t last_milli_lux;
    uint32_t measurements_started;
    uint32_t measurements_ok;
    uint32_t no_device_failures;
    uint32_t io_failures;
    uint32_t optional_retry_backoffs;
    uint32_t optional_retry_skips;
    uint32_t deadline_skips;
    uint32_t noncanonical_ticks_ignored;
} ev_bh1750_actor_ctx_t;

ev_result_t ev_bh1750_actor_init(ev_bh1750_actor_ctx_t *ctx,
                                 ev_i2c_port_t *i2c_port,
                                 ev_i2c_port_num_t port_num,
                                 uint8_t addr7,
                                 ev_delivery_fn_t deliver,
                                 void *deliver_context);

ev_result_t ev_bh1750_actor_configure_mode(ev_bh1750_actor_ctx_t *ctx,
                                           ev_bh1750_mode_t mode);

ev_result_t ev_bh1750_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_BH1750_ACTOR_H */
