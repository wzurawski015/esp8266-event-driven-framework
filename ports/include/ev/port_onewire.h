#ifndef EV_PORT_ONEWIRE_H
#define EV_PORT_ONEWIRE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Normalized outcome of one bounded 1-Wire bus primitive.
 */
typedef enum ev_onewire_status {
    EV_ONEWIRE_OK = 0, /**< Primitive completed successfully. */
    EV_ONEWIRE_ERR_NO_DEVICE = 1, /**< No slave presence pulse was observed on reset. */
    EV_ONEWIRE_ERR_BUS = 2 /**< The bus could not be driven or sampled safely. */
} ev_onewire_status_t;

/**
 * @brief Emit one 1-Wire reset pulse and sample the presence response.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @return EV_ONEWIRE_OK when one slave acknowledged the reset pulse.
 */
typedef ev_onewire_status_t (*ev_onewire_reset_fn_t)(void *ctx);

/**
 * @brief Write one byte on the active 1-Wire bus.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param value Byte value to send, LSB first.
 * @return Normalized transport status for the primitive.
 */
typedef ev_onewire_status_t (*ev_onewire_write_byte_fn_t)(void *ctx, uint8_t value);

/**
 * @brief Read one byte from the active 1-Wire bus.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param out_value Destination byte sampled from the bus.
 * @return Normalized transport status for the primitive.
 */
typedef ev_onewire_status_t (*ev_onewire_read_byte_fn_t)(void *ctx, uint8_t *out_value);

/**
 * @brief Platform 1-Wire master contract.
 */
typedef struct ev_onewire_port {
    void *ctx; /**< Caller-owned adapter context bound by the implementation. */
    ev_onewire_reset_fn_t reset; /**< Emit reset and sample presence. */
    ev_onewire_write_byte_fn_t write_byte; /**< Write one byte, LSB first. */
    ev_onewire_read_byte_fn_t read_byte; /**< Read one byte, LSB first. */
} ev_onewire_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_ONEWIRE_H */
