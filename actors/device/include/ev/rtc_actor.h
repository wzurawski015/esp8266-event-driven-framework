#ifndef EV_RTC_ACTOR_H
#define EV_RTC_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_RTC_DEFAULT_ADDR_7BIT 0x68U

/**
 * @brief Inline payload published when the DS3231 time source has been sampled.
 *
 * The payload transports both a decoded calendar/time snapshot and the
 * corresponding UNIX timestamp derived inside the actor.
 */
typedef struct {
    uint32_t unix_time;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} ev_time_payload_t;

EV_STATIC_ASSERT(sizeof(ev_time_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "RTC time payload must fit into one inline event payload");

/**
 * @brief Actor-local RTC state and injected dependencies.
 */
typedef struct {
    ev_i2c_port_t *i2c_port;
    ev_irq_port_t *irq_port;
    ev_i2c_port_num_t port_num;
    uint8_t device_address_7bit;
    ev_irq_line_id_t sqw_line_id;
    ev_delivery_fn_t deliver;
    void *deliver_context;
    bool sqw_enabled;
    bool time_valid;
    ev_time_payload_t last_time;
    uint32_t irq_samples_seen;
    uint32_t published_updates;
    uint32_t fallback_polls;
    uint32_t read_failures;
    uint32_t ticks_since_last_irq;
} ev_rtc_actor_ctx_t;

/**
 * @brief Initialize one RTC actor context.
 *
 * The actor performs no hardware I/O during initialization. It stores the
 * injected I2C contract, the logical IRQ ingress line carrying the DS3231 1 Hz
 * SQW signal, and the delivery contract used later to publish EV_TIME_UPDATED
 * whenever a matching EV_GPIO_IRQ sample is observed.
 *
 * @param ctx Context to initialize.
 * @param i2c_port Injected platform I2C contract.
 * @param irq_port Injected platform IRQ ingress contract used to arm the SQW line.
 * @param port_num Logical I2C controller number.
 * @param device_address_7bit Target 7-bit RTC I2C address.
 * @param sqw_line_id Logical IRQ line identifier used by the DS3231 SQW pin.
 * @param deliver Delivery callback used by ev_publish().
 * @param deliver_context Caller-owned context bound to @p deliver.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_rtc_actor_init(ev_rtc_actor_ctx_t *ctx,
                              ev_i2c_port_t *i2c_port,
                              ev_irq_port_t *irq_port,
                              ev_i2c_port_num_t port_num,
                              uint8_t device_address_7bit,
                              ev_irq_line_id_t sqw_line_id,
                              ev_delivery_fn_t deliver,
                              void *deliver_context);

/**
 * @brief Default actor handler for one RTC runtime instance.
 *
 * Supported events:
 * - EV_BOOT_COMPLETED
 * - EV_MCP23008_READY
 * - EV_GPIO_IRQ
 * - EV_TICK_1S
 *
 * The actor publishes EV_RTC_READY after the first successful time read.
 *
 * @param actor_context Pointer to ev_rtc_actor_ctx_t.
 * @param msg Runtime envelope delivered to the actor.
 * @return EV_OK on success or an error code when the message contract is invalid.
 */
ev_result_t ev_rtc_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_RTC_ACTOR_H */
