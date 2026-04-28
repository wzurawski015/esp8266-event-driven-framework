#ifndef EV_PORT_I2C_H
#define EV_PORT_I2C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical I2C controller identifier.
 *
 * The public contract treats the controller number as a stable logical
 * identifier. Concrete platform adapters translate this logical number into
 * the hardware resources selected by the active board profile.
 */
typedef uint8_t ev_i2c_port_num_t;

/**
 * @brief Canonical identifier for the first public I2C controller.
 */
#define EV_I2C_PORT_NUM_0 ((ev_i2c_port_num_t)0U)

/**
 * @brief Normalized outcome of one bounded I2C transaction.
 *
 * The status vocabulary intentionally stays narrow so portable application
 * code can react deterministically without depending on platform-specific
 * error numbers.
 */
typedef enum ev_i2c_status {
    EV_I2C_OK = 0, /**< The transaction completed successfully. */
    EV_I2C_ERR_TIMEOUT = 1, /**< The transaction exceeded the configured bounded wait time. */
    EV_I2C_ERR_NACK = 2, /**< The slave address or one transfer phase was not acknowledged. */
    EV_I2C_ERR_BUS_LOCKED = 3 /**< The bus could not be acquired or used safely. */
} ev_i2c_status_t;

/**
 * @brief Write one raw byte stream to a 7-bit addressed I2C slave.
 *
 * The operation emits a START condition, the target address in write mode,
 * @p data_len payload bytes, and a STOP condition. Passing a zero-length
 * payload is allowed and performs an address-only probe.
 * Implementations must use a bounded wait policy and must never block indefinitely.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param port_num Logical I2C controller identifier.
 * @param device_address_7bit Target 7-bit slave address.
 * @param data Pointer to payload bytes, or `NULL` when @p data_len is zero.
 * @param data_len Number of payload bytes to write.
 * @return Normalized transport status for the transaction.
 */
typedef ev_i2c_status_t (*ev_i2c_write_stream_fn_t)(void *ctx,
                                                    ev_i2c_port_num_t port_num,
                                                    uint8_t device_address_7bit,
                                                    const uint8_t *data,
                                                    size_t data_len);

/**
 * @brief Write one or more bytes starting at an 8-bit device register.
 *
 * The operation emits a START condition, the target address in write mode,
 * one register selector byte, @p data_len payload bytes, and a STOP
 * condition. Passing a zero-length payload is allowed and writes only the
 * register selector byte.
 * Implementations must use a bounded wait policy and must never block indefinitely.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param port_num Logical I2C controller identifier.
 * @param device_address_7bit Target 7-bit slave address.
 * @param first_reg First 8-bit register selector to write.
 * @param data Pointer to payload bytes, or `NULL` when @p data_len is zero.
 * @param data_len Number of payload bytes to write.
 * @return Normalized transport status for the transaction.
 */
typedef ev_i2c_status_t (*ev_i2c_write_regs_fn_t)(void *ctx,
                                                  ev_i2c_port_num_t port_num,
                                                  uint8_t device_address_7bit,
                                                  uint8_t first_reg,
                                                  const uint8_t *data,
                                                  size_t data_len);

/**
 * @brief Read one or more bytes starting at an 8-bit device register.
 *
 * The operation emits a START condition, the target address in write mode,
 * one register selector byte, a repeated START, the target address in read
 * mode, @p data_len input bytes, and a STOP condition.
 * Implementations must use a bounded wait policy and must never block indefinitely.
 *
 * @param ctx Adapter-owned context bound into the public port object.
 * @param port_num Logical I2C controller identifier.
 * @param device_address_7bit Target 7-bit slave address.
 * @param first_reg First 8-bit register selector to read.
 * @param data Destination buffer that receives the input bytes.
 * @param data_len Number of bytes to read.
 * @return Normalized transport status for the transaction.
 */
typedef ev_i2c_status_t (*ev_i2c_read_regs_fn_t)(void *ctx,
                                                 ev_i2c_port_num_t port_num,
                                                 uint8_t device_address_7bit,
                                                 uint8_t first_reg,
                                                 uint8_t *data,
                                                 size_t data_len);

/**
 * @brief Platform I2C master contract.
 *
 * The public contract exposes only the operations required by the current
 * bring-up stages: raw stream writes, register writes, and register reads.
 * Each call is expected to be bounded in time and safe to use from concurrent
 * callers through adapter-owned synchronization.
 */
typedef struct ev_i2c_port {
    void *ctx; /**< Caller-owned adapter context bound by the implementation. */
    ev_i2c_write_stream_fn_t write_stream; /**< Write a raw byte stream to one slave. */
    ev_i2c_write_regs_fn_t write_regs; /**< Write one or more device registers. */
    ev_i2c_read_regs_fn_t read_regs; /**< Read one or more device registers. */
} ev_i2c_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_I2C_H */
