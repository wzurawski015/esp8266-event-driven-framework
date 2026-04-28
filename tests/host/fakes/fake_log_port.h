#ifndef TESTS_HOST_FAKES_FAKE_LOG_PORT_H
#define TESTS_HOST_FAKES_FAKE_LOG_PORT_H

#include <stdint.h>

#include "ev/port_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t write_calls;
    uint32_t flush_calls;
    uint32_t call_sequence;
    uint32_t *shared_sequence;
    uint32_t write_order;
    uint32_t flush_order;
    ev_result_t next_write_result;
    ev_result_t next_flush_result;
} fake_log_port_t;

void fake_log_port_init(fake_log_port_t *fake);
void fake_log_port_bind(ev_log_port_t *out_port, fake_log_port_t *fake);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_LOG_PORT_H */
