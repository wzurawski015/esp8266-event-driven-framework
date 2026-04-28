#ifndef TESTS_HOST_FAKES_FAKE_SYSTEM_PORT_H
#define TESTS_HOST_FAKES_FAKE_SYSTEM_PORT_H

#include <stdint.h>

#include "ev/system_port.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t prepare_for_sleep_calls;
    uint32_t cancel_sleep_prepare_calls;
    uint32_t deep_sleep_calls;
    uint32_t prepare_order;
    uint32_t cancel_order;
    uint32_t deep_sleep_order;
    uint32_t call_sequence;
    uint32_t *external_sequence;
    uint64_t last_prepare_duration_us;
    uint64_t last_duration_us;
    ev_result_t next_prepare_result;
    ev_result_t next_cancel_result;
    ev_result_t next_result;
} fake_system_port_t;

void fake_system_port_init(fake_system_port_t *fake);
void fake_system_port_bind(ev_system_port_t *out_port, fake_system_port_t *fake);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_SYSTEM_PORT_H */
