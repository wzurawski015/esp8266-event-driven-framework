#include "ev/bh1750_driver.h"

#include <stddef.h>
#include <stdint.h>

#define EV_BH1750_CMD_POWER_DOWN 0x00U
#define EV_BH1750_CMD_POWER_ON 0x01U
#define EV_BH1750_READ_BYTES 2U

static ev_result_t ev_bh1750_map_i2c_status(ev_i2c_status_t status)
{
    switch (status) {
    case EV_I2C_OK:
        return EV_OK;
    case EV_I2C_ERR_NACK:
        return EV_ERR_NOT_FOUND;
    case EV_I2C_ERR_TIMEOUT:
        return EV_ERR_TIMEOUT;
    case EV_I2C_ERR_BUS_LOCKED:
    default:
        return EV_ERR_STATE;
    }
}

static ev_result_t ev_bh1750_port_is_valid(const ev_i2c_port_t *i2c_port)
{
    if ((i2c_port == NULL) || (i2c_port->ctx == NULL) ||
        (i2c_port->write_stream == NULL) || (i2c_port->read_stream == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

ev_result_t ev_bh1750_validate_addr7(uint8_t addr7)
{
    if ((addr7 == EV_BH1750_ADDR_LOW_7BIT) || (addr7 == EV_BH1750_ADDR_HIGH_7BIT)) {
        return EV_OK;
    }
    if (addr7 >= 128U) {
        return EV_ERR_OUT_OF_RANGE;
    }
    return EV_ERR_INVALID_ARG;
}

ev_result_t ev_bh1750_validate_mode(ev_bh1750_mode_t mode)
{
    switch (mode) {
    case EV_BH1750_MODE_CONTINUOUS_HIGH_RES:
    case EV_BH1750_MODE_CONTINUOUS_HIGH_RES_2:
    case EV_BH1750_MODE_CONTINUOUS_LOW_RES:
    case EV_BH1750_MODE_ONE_TIME_HIGH_RES:
    case EV_BH1750_MODE_ONE_TIME_HIGH_RES_2:
    case EV_BH1750_MODE_ONE_TIME_LOW_RES:
        return EV_OK;
    default:
        return EV_ERR_INVALID_ARG;
    }
}

static ev_result_t ev_bh1750_write_command(const ev_i2c_port_t *i2c_port,
                                           ev_i2c_port_num_t port_num,
                                           uint8_t addr7,
                                           uint8_t command)
{
    ev_result_t rc = ev_bh1750_port_is_valid(i2c_port);
    ev_i2c_status_t status;

    if (rc != EV_OK) {
        return rc;
    }
    rc = ev_bh1750_validate_addr7(addr7);
    if (rc != EV_OK) {
        return rc;
    }

    status = i2c_port->write_stream(i2c_port->ctx, port_num, addr7, &command, 1U);
    return ev_bh1750_map_i2c_status(status);
}

ev_result_t ev_bh1750_power_on(const ev_i2c_port_t *i2c_port,
                               ev_i2c_port_num_t port_num,
                               uint8_t addr7)
{
    return ev_bh1750_write_command(i2c_port, port_num, addr7, EV_BH1750_CMD_POWER_ON);
}

ev_result_t ev_bh1750_power_down(const ev_i2c_port_t *i2c_port,
                                 ev_i2c_port_num_t port_num,
                                 uint8_t addr7)
{
    return ev_bh1750_write_command(i2c_port, port_num, addr7, EV_BH1750_CMD_POWER_DOWN);
}

ev_result_t ev_bh1750_start_measurement(const ev_i2c_port_t *i2c_port,
                                         ev_i2c_port_num_t port_num,
                                         uint8_t addr7,
                                         ev_bh1750_mode_t mode)
{
    ev_result_t rc = ev_bh1750_validate_mode(mode);
    if (rc != EV_OK) {
        return rc;
    }
    return ev_bh1750_write_command(i2c_port, port_num, addr7, (uint8_t)mode);
}

ev_result_t ev_bh1750_read_measurement(const ev_i2c_port_t *i2c_port,
                                        ev_i2c_port_num_t port_num,
                                        uint8_t addr7,
                                        uint16_t *out_raw,
                                        uint32_t *out_milli_lux)
{
    uint8_t bytes[EV_BH1750_READ_BYTES] = {0U, 0U};
    ev_result_t rc = ev_bh1750_port_is_valid(i2c_port);
    ev_i2c_status_t status;
    uint16_t raw;

    if (rc != EV_OK) {
        return rc;
    }
    if ((out_raw == NULL) || (out_milli_lux == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_bh1750_validate_addr7(addr7);
    if (rc != EV_OK) {
        return rc;
    }

    status = i2c_port->read_stream(i2c_port->ctx, port_num, addr7, bytes, sizeof(bytes));
    rc = ev_bh1750_map_i2c_status(status);
    if (rc != EV_OK) {
        return rc;
    }

    raw = (uint16_t)(((uint16_t)bytes[0] << 8U) | (uint16_t)bytes[1]);
    *out_raw = raw;
    *out_milli_lux = ev_bh1750_raw_to_milli_lux(raw);
    return EV_OK;
}

uint16_t ev_bh1750_measurement_wait_ms(ev_bh1750_mode_t mode)
{
    switch (mode) {
    case EV_BH1750_MODE_CONTINUOUS_LOW_RES:
    case EV_BH1750_MODE_ONE_TIME_LOW_RES:
        return 24U;
    case EV_BH1750_MODE_CONTINUOUS_HIGH_RES:
    case EV_BH1750_MODE_CONTINUOUS_HIGH_RES_2:
    case EV_BH1750_MODE_ONE_TIME_HIGH_RES:
    case EV_BH1750_MODE_ONE_TIME_HIGH_RES_2:
        return 180U;
    default:
        return 0U;
    }
}

uint32_t ev_bh1750_raw_to_milli_lux(uint16_t raw)
{
    return ((uint32_t)raw * 5000U + 3U) / 6U;
}
