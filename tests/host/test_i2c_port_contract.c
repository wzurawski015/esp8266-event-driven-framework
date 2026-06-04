#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/port_i2c.h"
#include "fakes/fake_i2c_port.h"

int main(void)
{
    fake_i2c_port_t fake;
    ev_i2c_port_t port;
    const uint8_t addr = 0x23U;
    const uint8_t stream_seed[] = {0x12U, 0x34U, 0x56U};
    const uint8_t reg_seed[] = {0xA5U};
    const uint8_t reg_write[] = {0x5AU};
    uint8_t rx[sizeof(stream_seed)] = {0};
    uint8_t reg_rx = 0U;

    fake_i2c_port_init(&fake);
    fake_i2c_port_bind(&port, &fake);

    assert(port.ctx == &fake);
    assert(port.write_stream != NULL);
    assert(port.read_stream != NULL);
    assert(port.write_regs != NULL);
    assert(port.read_regs != NULL);

    fake_i2c_port_set_present(&fake, addr, true);
    fake_i2c_port_seed_read_stream(&fake, addr, stream_seed, sizeof(stream_seed));

    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, addr, rx, sizeof(rx)) == EV_I2C_OK);
    assert(memcmp(rx, stream_seed, sizeof(stream_seed)) == 0);
    assert(fake.read_stream_calls == 1U);
    assert(fake.read_stream_calls_by_addr[addr] == 1U);
    assert(fake.last_addr == addr);

    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, addr, NULL, 0U) == EV_I2C_OK);
    assert(fake.read_stream_calls == 2U);

    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, 0x24U, NULL, 0U) == EV_I2C_ERR_NACK);
    assert(fake.read_stream_calls_by_addr[0x24U] == 1U);

    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, addr, NULL, 1U) == EV_I2C_ERR_BUS_LOCKED);
    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, 0x80U, rx, 1U) == EV_I2C_ERR_BUS_LOCKED);
    assert(port.read_stream(port.ctx, EV_I2C_PORT_NUM_0, addr, rx, FAKE_I2C_STREAM_CAPACITY + 1U) == EV_I2C_ERR_BUS_LOCKED);

    fake_i2c_port_seed_regs(&fake, addr, 0xFFU, reg_seed, sizeof(reg_seed));
    assert(port.read_regs(port.ctx, EV_I2C_PORT_NUM_0, addr, 0xFFU, &reg_rx, 1U) == EV_I2C_OK);
    assert(reg_rx == reg_seed[0]);

    assert(port.write_regs(port.ctx, EV_I2C_PORT_NUM_0, addr, 0xFFU, reg_write, sizeof(reg_write)) == EV_I2C_OK);
    reg_rx = 0U;
    assert(port.read_regs(port.ctx, EV_I2C_PORT_NUM_0, addr, 0xFFU, &reg_rx, 1U) == EV_I2C_OK);
    assert(reg_rx == reg_write[0]);

    assert(port.write_regs(port.ctx, EV_I2C_PORT_NUM_0, addr, 0xFFU, reg_write, 2U) == EV_I2C_ERR_BUS_LOCKED);
    assert(port.read_regs(port.ctx, EV_I2C_PORT_NUM_0, addr, 0xFFU, rx, 2U) == EV_I2C_ERR_BUS_LOCKED);

    return 0;
}
