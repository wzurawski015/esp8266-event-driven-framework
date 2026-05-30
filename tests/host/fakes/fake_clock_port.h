#ifndef TESTS_HOST_FAKES_FAKE_CLOCK_PORT_H
#define TESTS_HOST_FAKES_FAKE_CLOCK_PORT_H

#include <stdint.h>

#include "ev/port_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ev_time_mono_us_t mono_now_us;
    ev_time_wall_us_t wall_now_us;
    ev_time_mono_us_t mono_step_us;
    uint32_t mono_calls;
    uint32_t wall_calls;
    uint32_t delay_calls;
    uint32_t last_delay_ms;
    ev_result_t next_mono_result;
    ev_result_t next_wall_result;
    ev_result_t next_delay_result;
} fake_clock_port_t;

void fake_clock_port_init(fake_clock_port_t *fake);
void fake_clock_port_bind(ev_clock_port_t *out_port, fake_clock_port_t *fake);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_CLOCK_PORT_H */
