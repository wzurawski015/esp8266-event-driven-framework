#ifndef EV_ESP8266_I2C_HIL_H
#define EV_ESP8266_I2C_HIL_H

#include <stdint.h>

#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Disabled GPIO sentinel used by the ESP8266 I2C zero-heap HIL suite.
 */
#define EV_ESP8266_I2C_HIL_GPIO_DISABLED (-1)

/**
 * @brief Immutable wiring and policy for the ESP8266 I2C zero-heap HIL suite.
 *
 * The suite performs bounded hardware-in-the-loop probes against the board I2C
 * devices and optional external fault-injection fixture.  Fixture GPIOs are
 * expected to be wired through open-drain safe circuitry to the target bus or
 * IRQ input; passing EV_ESP8266_I2C_HIL_GPIO_DISABLED skips the corresponding
 * destructive/fault-injection test instead of guessing hardware topology.
 */
typedef struct ev_esp8266_i2c_hil_config {
    const char *suite_name; /**< Human-readable suite name for logs. */
    const char *board_tag; /**< Stable board log tag. */
    ev_i2c_port_t *i2c_port; /**< Initialized zero-heap I2C port under test. */
    ev_irq_port_t *irq_port; /**< Optional initialized IRQ ingress port for flood testing. */
    ev_i2c_port_num_t i2c_port_num; /**< Logical I2C controller identifier. */
    uint8_t rtc_addr_7bit; /**< RTC 7-bit I2C address. */
    uint8_t mcp23008_addr_7bit; /**< MCP23008 7-bit I2C address. */
    uint8_t oled_addr_7bit; /**< OLED 7-bit I2C address. */
    uint8_t missing_addr_7bit; /**< Address expected to NACK on the HIL fixture. */
    uint32_t rtc_read_iterations; /**< Number of RTC register-read probes. */
    uint32_t mcp_rw_iterations; /**< Number of MCP23008 read/write probes. */
    uint32_t oled_partial_flushes; /**< Number of bounded OLED partial flush probes. */
    uint32_t oled_full_flushes; /**< Number of complete OLED scene flush probes. */
    uint32_t irq_flood_i2c_transactions; /**< I2C transactions to run during IRQ flood. */
    int sda_fault_gpio; /**< Optional fixture GPIO wired to pull SDA low. */
    int scl_fault_gpio; /**< Optional fixture GPIO wired to pull SCL low. */
    int irq_flood_output_gpio; /**< Optional fixture GPIO wired to an IRQ input line. */
    ev_irq_line_id_t irq_flood_line_id; /**< IRQ line id to arm during flood testing. */
} ev_esp8266_i2c_hil_config_t;

/**
 * @brief Run the ESP8266 zero-heap I2C hardware-in-the-loop validation suite.
 *
 * The suite is intended for explicit firmware builds compiled with
 * EV_HIL_I2C_ZERO_HEAP=1.  It never changes the public I2C port contract and it
 * treats fault-injection tests without configured fixture GPIOs as skipped, not
 * passed.  The return value is EV_OK only when every non-skipped test passes.
 *
 * @param cfg Immutable HIL configuration and initialized ports.
 * @return EV_OK when all executed tests passed, or EV_ERR_STATE/EV_ERR_INVALID_ARG.
 */
ev_result_t ev_esp8266_i2c_zero_heap_hil_run(const ev_esp8266_i2c_hil_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* EV_ESP8266_I2C_HIL_H */
