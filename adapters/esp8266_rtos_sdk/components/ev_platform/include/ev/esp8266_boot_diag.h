#ifndef EV_ESP8266_BOOT_DIAG_H
#define EV_ESP8266_BOOT_DIAG_H

#include <stdint.h>

#include "ev/port_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Shared ESP8266 boot/diagnostic harness configuration.
 *
 * This helper keeps the generic reference target and the first board target on
 * the same framework-backed bring-up path.
 */
typedef struct ev_boot_diag_config {
    const char *board_tag;
    const char *board_name;
    ev_uart_port_num_t uart_port;
    uint32_t uart_baud_rate;
    uint32_t heartbeat_period_ms;
} ev_boot_diag_config_t;

/**
 * @brief Run the shared ESP8266 boot/diagnostic application loop.
 *
 * The helper initializes the current Stage 2 public platform ports and then
 * emits a small runtime heartbeat log forever.
 */
void ev_esp8266_boot_diag_run(const ev_boot_diag_config_t *cfg);

#ifdef __cplusplus
}
#endif

#endif /* EV_ESP8266_BOOT_DIAG_H */
