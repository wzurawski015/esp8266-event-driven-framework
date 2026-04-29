#ifndef EV_RUNTIME_PORTS_H
#define EV_RUNTIME_PORTS_H

#include "ev/port_clock.h"
#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/port_log.h"
#include "ev/port_net.h"
#include "ev/port_onewire.h"
#include "ev/port_reset.h"
#include "ev/port_uart.h"
#include "ev/port_wdt.h"
#include "ev/system_port.h"

/**
 * @brief Optional platform port bundle injected into runtime-built actors.
 *
 * The bundle is intentionally a set of pointers to portable port contracts.
 * Platform SDK types must stay inside adapters/ and target-specific code.
 */
typedef struct {
    ev_clock_port_t *clock;
    ev_log_port_t *log;
    ev_irq_port_t *irq;
    ev_i2c_port_t *i2c;
    ev_onewire_port_t *onewire;
    ev_system_port_t *system;
    ev_wdt_port_t *wdt;
    ev_net_port_t *net;
    ev_reset_port_t *reset;
    ev_uart_port_t *uart;
} ev_runtime_ports_t;

#endif /* EV_RUNTIME_PORTS_H */
