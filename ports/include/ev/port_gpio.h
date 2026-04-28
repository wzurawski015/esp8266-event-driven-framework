#ifndef EV_PORT_GPIO_H
#define EV_PORT_GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Logical GPIO identifier.
 */
typedef uint8_t ev_gpio_num_t;

/**
 * @brief Direction requested for one GPIO line.
 */
typedef enum ev_gpio_direction {
    EV_GPIO_INPUT = 0,
    EV_GPIO_OUTPUT = 1
} ev_gpio_direction_t;

/**
 * @brief Pull configuration requested for one GPIO line.
 */
typedef enum ev_gpio_pull {
    EV_GPIO_PULL_FLOATING = 0,
    EV_GPIO_PULL_UP = 1,
    EV_GPIO_PULL_DOWN = 2
} ev_gpio_pull_t;

/**
 * @brief Configuration for one GPIO line.
 */
typedef struct ev_gpio_config {
    ev_gpio_direction_t direction; /**< Requested input/output direction. */
    ev_gpio_pull_t pull; /**< Requested pull configuration. */
    bool open_drain; /**< Enable open-drain mode when supported. */
    bool initial_high; /**< Initial output level for output lines. */
} ev_gpio_config_t;

/**
 * @brief Platform GPIO contract.
 */
typedef struct ev_gpio_port {
    void *ctx; /**< Caller-owned adapter context. */
    ev_result_t (*configure)(void *ctx, ev_gpio_num_t pin, const ev_gpio_config_t *cfg); /**< Configure one pin. */
    ev_result_t (*write)(void *ctx, ev_gpio_num_t pin, bool high); /**< Drive one output level. */
    ev_result_t (*read)(void *ctx, ev_gpio_num_t pin, bool *out_high); /**< Sample one pin level. */
} ev_gpio_port_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_GPIO_H */
