#ifndef EV_DS18B20_ACTOR_H
#define EV_DS18B20_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/port_onewire.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inline payload published when the DS18B20 temperature has been sampled.
 */
typedef struct {
    int16_t centi_celsius;
} ev_temp_payload_t;

EV_STATIC_ASSERT(sizeof(ev_temp_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "DS18B20 temperature payload must fit into one inline event payload");

/**
 * @brief Actor-local DS18B20 state and injected dependencies.
 */
typedef struct {
    ev_onewire_port_t *onewire_port;
    ev_delivery_fn_t deliver;
    void *deliver_context;
    bool conversion_pending;
    bool sensor_present;
    bool last_read_ok;
    bool temp_valid;
    int16_t last_centi_celsius;
    uint32_t conversions_started;
    uint32_t scratchpad_reads_ok;
    uint32_t crc_failures;
    uint32_t no_device_failures;
    uint32_t io_failures;
} ev_ds18b20_actor_ctx_t;

/**
 * @brief Initialize one DS18B20 actor context.
 *
 * @param ctx Context to initialize.
 * @param onewire_port Injected 1-Wire transport contract.
 * @param deliver Delivery callback used by ev_publish().
 * @param deliver_context Caller-owned context bound to @p deliver.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_ds18b20_actor_init(ev_ds18b20_actor_ctx_t *ctx,
                                  ev_onewire_port_t *onewire_port,
                                  ev_delivery_fn_t deliver,
                                  void *deliver_context);

/**
 * @brief Default actor handler for one DS18B20 runtime instance.
 *
 * Supported events:
 * - EV_BOOT_COMPLETED
 * - EV_TICK_1S
 *
 * @param actor_context Pointer to ev_ds18b20_actor_ctx_t.
 * @param msg Runtime envelope delivered to the actor.
 * @return EV_OK on success or an error code when the message contract is invalid.
 */
ev_result_t ev_ds18b20_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_DS18B20_ACTOR_H */
