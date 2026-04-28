#ifndef EV_MCP23008_ACTOR_H
#define EV_MCP23008_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/port_i2c.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_MCP23008_DEFAULT_ADDR_7BIT 0x20U
#define EV_MCP23008_BUTTON_COUNT 4U
#define EV_MCP23008_BUTTON_MASK 0x0FU
#define EV_MCP23008_LED_MASK 0x0FU

/**
 * @brief Inline payload published when the normalized MCP23008 button state changes.
 *
 * Bits 0..3 map to GP0..GP3 after active-low normalization, so a set bit means
 * the corresponding button is currently pressed.
 */
typedef struct {
    uint8_t pressed_mask;
    uint8_t changed_mask;
} ev_mcp23008_input_payload_t;

EV_STATIC_ASSERT(sizeof(ev_mcp23008_input_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "MCP23008 input payload must fit into one inline event payload");

/**
 * @brief Inline payload consumed by EV_PANEL_LED_SET_CMD.
 *
 * Bits 0..3 represent the four logical panel LEDs. The hardware actor maps
 * them onto MCP23008 outputs GP4..GP7.
 */
typedef struct {
    uint8_t value_mask;
    uint8_t valid_mask;
} ev_panel_led_set_cmd_t;

EV_STATIC_ASSERT(sizeof(ev_panel_led_set_cmd_t) <= EV_MSG_INLINE_CAPACITY,
                 "Panel LED command must fit into one inline event payload");

/**
 * @brief Actor-local MCP23008 state and injected dependencies.
 */
typedef struct {
    ev_i2c_port_t *i2c_port;
    ev_i2c_port_num_t port_num;
    uint8_t device_address_7bit;
    ev_delivery_fn_t deliver;
    void *deliver_context;
    uint8_t input_shadow;
    uint8_t output_shadow;
    bool configured;
    bool inputs_valid;
} ev_mcp23008_actor_ctx_t;

/**
 * @brief Initialize one MCP23008 actor context.
 *
 * The actor performs no hardware I/O during initialization. It stores the
 * injected I2C contract and the delivery callback used later to publish
 * EV_MCP23008_INPUT_CHANGED.
 *
 * @param ctx Context to initialize.
 * @param i2c_port Injected platform I2C contract.
 * @param port_num Logical I2C controller number.
 * @param device_address_7bit Target 7-bit MCP23008 I2C address.
 * @param deliver Delivery callback used by ev_publish().
 * @param deliver_context Caller-owned context bound to @p deliver.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_mcp23008_actor_init(ev_mcp23008_actor_ctx_t *ctx,
                                   ev_i2c_port_t *i2c_port,
                                   ev_i2c_port_num_t port_num,
                                   uint8_t device_address_7bit,
                                   ev_delivery_fn_t deliver,
                                   void *deliver_context);

/**
 * @brief Default actor handler for one MCP23008 runtime instance.
 *
 * Supported events:
 * - EV_BOOT_COMPLETED
 * - EV_TICK_100MS
 * - EV_PANEL_LED_SET_CMD
 *
 * @param actor_context Pointer to ev_mcp23008_actor_ctx_t.
 * @param msg Runtime envelope delivered to the actor.
 * @return EV_OK on success or an error code when the message contract is invalid.
 */
ev_result_t ev_mcp23008_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_MCP23008_ACTOR_H */
