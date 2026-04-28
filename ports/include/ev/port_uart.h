#ifndef EV_PORT_UART_H
#define EV_PORT_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical UART identifier.
 */
typedef uint8_t ev_uart_port_num_t;

/**
 * @brief Line configuration for one UART port.
 */
typedef struct ev_uart_config {
    uint32_t baud_rate; /**< Requested baud rate. */
    uint8_t data_bits; /**< Number of data bits per frame. */
    uint8_t stop_bits; /**< Number of stop bits per frame. */
    bool parity_enable; /**< Enable parity generation/checking. */
    bool parity_odd; /**< Select odd parity when parity is enabled. */
} ev_uart_config_t;

/**
 * @brief Platform UART contract.
 */
typedef struct ev_uart_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*init)(void *ctx, ev_uart_port_num_t port, const ev_uart_config_t *cfg); /**< Initialize one UART port. */
    ev_result_t (*write)(void *ctx, ev_uart_port_num_t port, const void *data, size_t len, size_t *out_written); /**< Write raw bytes. */
    ev_result_t (*read)(void *ctx, ev_uart_port_num_t port, void *data, size_t len, size_t *out_read); /**< Read raw bytes. */
} ev_uart_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_UART_H */
