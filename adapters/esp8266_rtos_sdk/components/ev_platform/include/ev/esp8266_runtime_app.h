#ifndef EV_ESP8266_RUNTIME_APP_H
#define EV_ESP8266_RUNTIME_APP_H

#include "ev/esp8266_boot_diag.h"
#include "ev/demo_app.h"
#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/port_onewire.h"
#include "ev/port_wdt.h"
#include "ev/port_net.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Run the first real ESP8266 event-driven firmware loop.
 *
 * The ATNEL board target uses this helper to exercise the framework core,
 * static routing, deterministic lease payloads, and the cooperative system
 * pump on hardware instead of staying on the boot/diagnostic heartbeat path.
 *
 * @param cfg Immutable board/runtime configuration.
 * @param i2c_port Optional injected I2C port used by hardware actors.
 * @param irq_port Optional injected GPIO IRQ ingress port used by the runtime.
 * @param onewire_port Optional injected 1-Wire port used by hardware actors.
 * @param wdt_port Optional injected hardware watchdog mechanism.
 * @param net_port Optional injected bounded network ingress/egress mechanism.
 */
void ev_esp8266_runtime_app_run(const ev_boot_diag_config_t *cfg,
                                ev_i2c_port_t *i2c_port,
                                ev_irq_port_t *irq_port,
                                ev_onewire_port_t *onewire_port,
                                ev_wdt_port_t *wdt_port,
                                ev_net_port_t *net_port,
                                const ev_demo_app_board_profile_t *board_profile);

#ifdef __cplusplus
}
#endif

#endif /* EV_ESP8266_RUNTIME_APP_H */
