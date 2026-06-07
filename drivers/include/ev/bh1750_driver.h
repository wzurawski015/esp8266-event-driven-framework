#ifndef EV_BH1750_DRIVER_H
#define EV_BH1750_DRIVER_H

#include <stdint.h>

#include "ev/port_i2c.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_BH1750_ADDR_LOW_7BIT 0x23U
#define EV_BH1750_ADDR_HIGH_7BIT 0x5CU

typedef enum ev_bh1750_mode {
    EV_BH1750_MODE_CONTINUOUS_HIGH_RES = 0x10U,
    EV_BH1750_MODE_CONTINUOUS_HIGH_RES_2 = 0x11U,
    EV_BH1750_MODE_CONTINUOUS_LOW_RES = 0x13U,
    EV_BH1750_MODE_ONE_TIME_HIGH_RES = 0x20U,
    EV_BH1750_MODE_ONE_TIME_HIGH_RES_2 = 0x21U,
    EV_BH1750_MODE_ONE_TIME_LOW_RES = 0x23U
} ev_bh1750_mode_t;

ev_result_t ev_bh1750_power_on(const ev_i2c_port_t *i2c_port,
                               ev_i2c_port_num_t port_num,
                               uint8_t addr7);

ev_result_t ev_bh1750_power_down(const ev_i2c_port_t *i2c_port,
                                 ev_i2c_port_num_t port_num,
                                 uint8_t addr7);

ev_result_t ev_bh1750_start_measurement(const ev_i2c_port_t *i2c_port,
                                         ev_i2c_port_num_t port_num,
                                         uint8_t addr7,
                                         ev_bh1750_mode_t mode);

ev_result_t ev_bh1750_read_measurement(const ev_i2c_port_t *i2c_port,
                                        ev_i2c_port_num_t port_num,
                                        uint8_t addr7,
                                        uint16_t *out_raw,
                                        uint32_t *out_milli_lux);

uint16_t ev_bh1750_measurement_wait_ms(ev_bh1750_mode_t mode);
uint32_t ev_bh1750_raw_to_milli_lux(uint16_t raw);

ev_result_t ev_bh1750_validate_addr7(uint8_t addr7);
ev_result_t ev_bh1750_validate_mode(ev_bh1750_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* EV_BH1750_DRIVER_H */
