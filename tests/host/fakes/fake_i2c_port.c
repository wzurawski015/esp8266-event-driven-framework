#include "fake_i2c_port.h"

#include <string.h>

static ev_i2c_status_t fake_i2c_port_status_for(fake_i2c_port_t *fake, uint8_t addr_7bit)
{
    if ((fake == NULL) || (addr_7bit >= 128U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }
    if (!fake->present[addr_7bit]) {
        return EV_I2C_ERR_NACK;
    }
    if (fake->forced_status_valid[addr_7bit]) {
        return fake->status_by_addr[addr_7bit];
    }
    return fake->default_status;
}

static ev_i2c_status_t fake_i2c_write_stream(void *ctx,
                                             ev_i2c_port_num_t port_num,
                                             uint8_t device_address_7bit,
                                             const uint8_t *data,
                                             size_t data_len)
{
    fake_i2c_port_t *fake = (fake_i2c_port_t *)ctx;
    ev_i2c_status_t status;

    (void)port_num;
    if ((fake == NULL) || ((data == NULL) && (data_len != 0U)) || (device_address_7bit >= 128U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    ++fake->write_stream_calls;
    ++fake->write_stream_calls_by_addr[device_address_7bit];
    fake->last_addr = device_address_7bit;
    status = fake_i2c_port_status_for(fake, device_address_7bit);
    if (status != EV_I2C_OK) {
        return status;
    }

    if ((data_len >= 2U) && (data != NULL)) {
        uint8_t control = data[0];
        (void)control;
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t fake_i2c_write_regs(void *ctx,
                                           ev_i2c_port_num_t port_num,
                                           uint8_t device_address_7bit,
                                           uint8_t first_reg,
                                           const uint8_t *data,
                                           size_t data_len)
{
    fake_i2c_port_t *fake = (fake_i2c_port_t *)ctx;
    ev_i2c_status_t status;

    (void)port_num;
    if ((fake == NULL) || ((data == NULL) && (data_len != 0U)) || (device_address_7bit >= 128U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    ++fake->write_regs_calls;
    ++fake->write_regs_calls_by_addr[device_address_7bit];
    fake->last_addr = device_address_7bit;
    fake->last_reg = first_reg;
    status = fake_i2c_port_status_for(fake, device_address_7bit);
    if (status != EV_I2C_OK) {
        return status;
    }

    if ((data_len > 0U) && (data != NULL)) {
        memcpy(&fake->regs[device_address_7bit][first_reg], data, data_len);
    }

    return EV_I2C_OK;
}

static ev_i2c_status_t fake_i2c_read_regs(void *ctx,
                                          ev_i2c_port_num_t port_num,
                                          uint8_t device_address_7bit,
                                          uint8_t first_reg,
                                          uint8_t *data,
                                          size_t data_len)
{
    fake_i2c_port_t *fake = (fake_i2c_port_t *)ctx;
    ev_i2c_status_t status;

    (void)port_num;
    if ((fake == NULL) || (data == NULL) || (data_len == 0U) || (device_address_7bit >= 128U)) {
        return EV_I2C_ERR_BUS_LOCKED;
    }

    ++fake->read_regs_calls;
    ++fake->read_regs_calls_by_addr[device_address_7bit];
    fake->last_addr = device_address_7bit;
    fake->last_reg = first_reg;
    status = fake_i2c_port_status_for(fake, device_address_7bit);
    if (status != EV_I2C_OK) {
        return status;
    }

    memcpy(data, &fake->regs[device_address_7bit][first_reg], data_len);
    return EV_I2C_OK;
}

void fake_i2c_port_init(fake_i2c_port_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
        fake->default_status = EV_I2C_OK;
    }
}

void fake_i2c_port_bind(ev_i2c_port_t *out_port, fake_i2c_port_t *fake)
{
    if (out_port != NULL) {
        memset(out_port, 0, sizeof(*out_port));
        out_port->ctx = fake;
        out_port->write_stream = fake_i2c_write_stream;
        out_port->write_regs = fake_i2c_write_regs;
        out_port->read_regs = fake_i2c_read_regs;
    }
}

void fake_i2c_port_set_present(fake_i2c_port_t *fake, uint8_t addr_7bit, bool present)
{
    if ((fake != NULL) && (addr_7bit < 128U)) {
        fake->present[addr_7bit] = present;
    }
}

void fake_i2c_port_set_status(fake_i2c_port_t *fake, uint8_t addr_7bit, ev_i2c_status_t status)
{
    if ((fake != NULL) && (addr_7bit < 128U)) {
        fake->forced_status_valid[addr_7bit] = true;
        fake->status_by_addr[addr_7bit] = status;
    }
}

void fake_i2c_port_seed_regs(fake_i2c_port_t *fake, uint8_t addr_7bit, uint8_t first_reg, const uint8_t *src, size_t len)
{
    if ((fake != NULL) && (src != NULL) && (addr_7bit < 128U) && (len > 0U)) {
        memcpy(&fake->regs[addr_7bit][first_reg], src, len);
    }
}
