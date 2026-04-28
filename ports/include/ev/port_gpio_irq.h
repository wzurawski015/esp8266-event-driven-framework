#ifndef EV_PORT_GPIO_IRQ_H
#define EV_PORT_GPIO_IRQ_H

#include <stdint.h>

#include "ev/port_irq.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Adapter-level trigger modes supported by GPIO-backed IRQ lines.
 *
 * This contract is intentionally kept outside of Core. It exists only so BSP
 * and platform adapters can describe how one logical interrupt ingress line is
 * mapped onto a concrete GPIO pad.
 */
typedef uint8_t ev_gpio_irq_trigger_t;

#define EV_GPIO_IRQ_TRIGGER_RISING ((ev_gpio_irq_trigger_t)1U)
#define EV_GPIO_IRQ_TRIGGER_FALLING ((ev_gpio_irq_trigger_t)2U)
#define EV_GPIO_IRQ_TRIGGER_ANYEDGE ((ev_gpio_irq_trigger_t)3U)

/**
 * @brief Adapter-level pull configuration for GPIO-backed IRQ lines.
 */
typedef uint8_t ev_gpio_irq_pull_mode_t;

#define EV_GPIO_IRQ_PULL_NONE ((ev_gpio_irq_pull_mode_t)0U)
#define EV_GPIO_IRQ_PULL_UP ((ev_gpio_irq_pull_mode_t)1U)

/**
 * @brief Static mapping of one logical IRQ ingress line onto one GPIO pad.
 */
typedef struct {
    ev_irq_line_id_t line_id;
    uint8_t gpio_num;
    ev_gpio_irq_trigger_t trigger;
    ev_gpio_irq_pull_mode_t pull_mode;
} ev_gpio_irq_line_config_t;

#ifdef __cplusplus
}
#endif

#endif /* EV_PORT_GPIO_IRQ_H */
