#ifndef TESTS_HOST_FAKES_FAKE_ONEWIRE_PORT_H
#define TESTS_HOST_FAKES_FAKE_ONEWIRE_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/port_onewire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ev_onewire_status_t reset_status;
    ev_onewire_status_t write_status;
    ev_onewire_status_t read_status;
    bool present;
    uint8_t read_bytes[16];
    size_t read_len;
    size_t read_index;
    uint32_t reset_calls;
    uint32_t write_calls;
    uint32_t read_calls;
    uint8_t last_written[8];
    size_t last_written_len;
} fake_onewire_port_t;

void fake_onewire_port_init(fake_onewire_port_t *fake);
void fake_onewire_port_bind(ev_onewire_port_t *out_port, fake_onewire_port_t *fake);
void fake_onewire_port_seed_read_bytes(fake_onewire_port_t *fake, const uint8_t *src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_ONEWIRE_PORT_H */
