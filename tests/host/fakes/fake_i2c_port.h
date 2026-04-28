#ifndef TESTS_HOST_FAKES_FAKE_I2C_PORT_H
#define TESTS_HOST_FAKES_FAKE_I2C_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/port_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    ev_i2c_status_t default_status;
    ev_i2c_status_t status_by_addr[128];
    bool forced_status_valid[128];
    bool present[128];
    uint8_t regs[128][256];
    uint32_t write_stream_calls;
    uint32_t write_regs_calls;
    uint32_t read_regs_calls;
    uint32_t write_stream_calls_by_addr[128];
    uint32_t write_regs_calls_by_addr[128];
    uint32_t read_regs_calls_by_addr[128];
    uint8_t last_addr;
    uint8_t last_reg;
} fake_i2c_port_t;

void fake_i2c_port_init(fake_i2c_port_t *fake);
void fake_i2c_port_bind(ev_i2c_port_t *out_port, fake_i2c_port_t *fake);
void fake_i2c_port_set_present(fake_i2c_port_t *fake, uint8_t addr_7bit, bool present);
void fake_i2c_port_set_status(fake_i2c_port_t *fake, uint8_t addr_7bit, ev_i2c_status_t status);
void fake_i2c_port_seed_regs(fake_i2c_port_t *fake, uint8_t addr_7bit, uint8_t first_reg, const uint8_t *src, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_I2C_PORT_H */
