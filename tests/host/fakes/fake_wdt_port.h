#ifndef TESTS_HOST_FAKES_FAKE_WDT_PORT_H
#define TESTS_HOST_FAKES_FAKE_WDT_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/port_wdt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fake_wdt_port {
    uint32_t enable_calls;
    uint32_t feed_calls;
    uint32_t is_supported_calls;
    uint32_t get_reset_reason_calls;
    uint32_t last_timeout_ms;
    ev_result_t next_enable_result;
    ev_result_t next_feed_result;
    ev_result_t next_is_supported_result;
    ev_result_t next_get_reset_reason_result;
    ev_reset_reason_t reset_reason;
    bool supported;
} fake_wdt_port_t;

void fake_wdt_port_init(fake_wdt_port_t *fake);
void fake_wdt_port_bind(ev_wdt_port_t *out_port, fake_wdt_port_t *fake);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_WDT_PORT_H */
