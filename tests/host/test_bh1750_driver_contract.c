#include <assert.h>
#include <stdint.h>

#include "ev/bh1750_driver.h"
#include "fakes/fake_i2c_port.h"

int main(void)
{
    fake_i2c_port_t fake;
    ev_i2c_port_t port;
    const uint8_t addr = EV_BH1750_ADDR_LOW_7BIT;
    const uint8_t raw_400_lux[] = {0x01U, 0xE0U};
    uint16_t raw = 0U;
    uint32_t milli_lux = 0U;

    fake_i2c_port_init(&fake);
    fake_i2c_port_bind(&port, &fake);
    fake_i2c_port_set_present(&fake, addr, true);
    fake_i2c_port_seed_read_stream(&fake, addr, raw_400_lux, sizeof(raw_400_lux));

    assert(ev_bh1750_validate_addr7(EV_BH1750_ADDR_LOW_7BIT) == EV_OK);
    assert(ev_bh1750_validate_addr7(EV_BH1750_ADDR_HIGH_7BIT) == EV_OK);
    assert(ev_bh1750_validate_addr7(0x80U) == EV_ERR_OUT_OF_RANGE);
    assert(ev_bh1750_validate_addr7(0x42U) == EV_ERR_INVALID_ARG);
    assert(ev_bh1750_validate_mode(EV_BH1750_MODE_ONE_TIME_HIGH_RES) == EV_OK);
    assert(ev_bh1750_validate_mode((ev_bh1750_mode_t)0xFFU) == EV_ERR_INVALID_ARG);

    assert(ev_bh1750_measurement_wait_ms(EV_BH1750_MODE_ONE_TIME_LOW_RES) == 24U);
    assert(ev_bh1750_measurement_wait_ms(EV_BH1750_MODE_ONE_TIME_HIGH_RES) == 180U);
    assert(ev_bh1750_measurement_wait_ms((ev_bh1750_mode_t)0xFFU) == 0U);
    assert(ev_bh1750_raw_to_milli_lux(480U) == 400000U);

    assert(ev_bh1750_power_on(&port, EV_I2C_PORT_NUM_0, addr) == EV_OK);
    assert(fake.write_stream_calls == 1U);
    assert(fake.last_write_stream_len == 1U);
    assert(fake.last_write_stream_data[0] == 0x01U);

    assert(ev_bh1750_start_measurement(&port, EV_I2C_PORT_NUM_0, addr, EV_BH1750_MODE_ONE_TIME_HIGH_RES) == EV_OK);
    assert(fake.write_stream_calls == 2U);
    assert(fake.last_write_stream_data[0] == (uint8_t)EV_BH1750_MODE_ONE_TIME_HIGH_RES);

    assert(ev_bh1750_read_measurement(&port, EV_I2C_PORT_NUM_0, addr, &raw, &milli_lux) == EV_OK);
    assert(fake.read_stream_calls == 1U);
    assert(raw == 480U);
    assert(milli_lux == 400000U);

    assert(ev_bh1750_read_measurement(&port, EV_I2C_PORT_NUM_0, addr, NULL, &milli_lux) == EV_ERR_INVALID_ARG);
    assert(ev_bh1750_read_measurement(&port, EV_I2C_PORT_NUM_0, addr, &raw, NULL) == EV_ERR_INVALID_ARG);
    assert(ev_bh1750_start_measurement(NULL, EV_I2C_PORT_NUM_0, addr, EV_BH1750_MODE_ONE_TIME_HIGH_RES) == EV_ERR_INVALID_ARG);
    assert(ev_bh1750_start_measurement(&port, EV_I2C_PORT_NUM_0, 0x5DU, EV_BH1750_MODE_ONE_TIME_HIGH_RES) == EV_ERR_INVALID_ARG);

    fake_i2c_port_set_present(&fake, addr, false);
    assert(ev_bh1750_read_measurement(&port, EV_I2C_PORT_NUM_0, addr, &raw, &milli_lux) == EV_ERR_NOT_FOUND);

    return 0;
}
