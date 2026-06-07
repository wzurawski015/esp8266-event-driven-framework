#include "ev/ds18b20_driver.h"

#include <stddef.h>
#include <stdint.h>

#define EV_DS18B20_CMD_SKIP_ROM 0xCCU
#define EV_DS18B20_CMD_CONVERT_T 0x44U
#define EV_DS18B20_CMD_READ_SCRATCHPAD 0xBEU
#define EV_DS18B20_CFG_RESOLUTION_MASK 0x60U
#define EV_DS18B20_CFG_9BIT 0x00U
#define EV_DS18B20_CFG_10BIT 0x20U
#define EV_DS18B20_CFG_11BIT 0x40U
#define EV_DS18B20_CFG_12BIT 0x60U
#define EV_DS18B20_CRC_POLY 0x8CU

static ev_result_t ev_ds18b20_map_onewire_status(ev_onewire_status_t status)
{
    if (status == EV_ONEWIRE_OK) {
        return EV_OK;
    }
    if (status == EV_ONEWIRE_ERR_NO_DEVICE) {
        return EV_ERR_NOT_FOUND;
    }
    return EV_ERR_STATE;
}

static ev_result_t ev_ds18b20_port_is_valid(const ev_onewire_port_t *onewire_port)
{
    if ((onewire_port == NULL) || (onewire_port->ctx == NULL) ||
        (onewire_port->reset == NULL) || (onewire_port->write_byte == NULL) ||
        (onewire_port->read_byte == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}

uint8_t ev_ds18b20_crc8(const uint8_t *data, size_t data_len)
{
    uint8_t crc = 0U;
    size_t i;

    if (data == NULL) {
        return 0U;
    }

    for (i = 0U; i < data_len; ++i) {
        uint8_t current = data[i];
        uint8_t bit;

        for (bit = 0U; bit < 8U; ++bit) {
            const uint8_t mix = (uint8_t)((crc ^ current) & 0x01U);
            crc = (uint8_t)(crc >> 1U);
            if (mix != 0U) {
                crc ^= EV_DS18B20_CRC_POLY;
            }
            current = (uint8_t)(current >> 1U);
        }
    }

    return crc;
}

uint16_t ev_ds18b20_conversion_time_ms(uint8_t resolution_bits)
{
    switch (resolution_bits) {
    case 9U:
        return 94U;
    case 10U:
        return 188U;
    case 11U:
        return 375U;
    case 12U:
        return 750U;
    default:
        return 750U;
    }
}

ev_result_t ev_ds18b20_start_conversion_skip_rom(const ev_onewire_port_t *onewire_port)
{
    ev_onewire_status_t status;
    ev_result_t rc = ev_ds18b20_port_is_valid(onewire_port);

    if (rc != EV_OK) {
        return rc;
    }

    status = onewire_port->reset(onewire_port->ctx);
    if (status != EV_ONEWIRE_OK) {
        return ev_ds18b20_map_onewire_status(status);
    }
    status = onewire_port->write_byte(onewire_port->ctx, EV_DS18B20_CMD_SKIP_ROM);
    if (status != EV_ONEWIRE_OK) {
        return ev_ds18b20_map_onewire_status(status);
    }
    status = onewire_port->write_byte(onewire_port->ctx, EV_DS18B20_CMD_CONVERT_T);
    return ev_ds18b20_map_onewire_status(status);
}

ev_result_t ev_ds18b20_read_scratchpad_skip_rom(const ev_onewire_port_t *onewire_port,
                                                uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES])
{
    ev_onewire_status_t status;
    ev_result_t rc = ev_ds18b20_port_is_valid(onewire_port);
    size_t i;

    if (rc != EV_OK) {
        return rc;
    }
    if (scratchpad == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    status = onewire_port->reset(onewire_port->ctx);
    if (status != EV_ONEWIRE_OK) {
        return ev_ds18b20_map_onewire_status(status);
    }
    status = onewire_port->write_byte(onewire_port->ctx, EV_DS18B20_CMD_SKIP_ROM);
    if (status != EV_ONEWIRE_OK) {
        return ev_ds18b20_map_onewire_status(status);
    }
    status = onewire_port->write_byte(onewire_port->ctx, EV_DS18B20_CMD_READ_SCRATCHPAD);
    if (status != EV_ONEWIRE_OK) {
        return ev_ds18b20_map_onewire_status(status);
    }

    for (i = 0U; i < EV_DS18B20_SCRATCHPAD_BYTES; ++i) {
        status = onewire_port->read_byte(onewire_port->ctx, &scratchpad[i]);
        if (status != EV_ONEWIRE_OK) {
            return ev_ds18b20_map_onewire_status(status);
        }
    }

    return ev_ds18b20_validate_scratchpad(scratchpad);
}

ev_result_t ev_ds18b20_validate_scratchpad(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES])
{
    if (scratchpad == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_ds18b20_crc8(scratchpad, EV_DS18B20_SCRATCHPAD_BYTES - 1U) !=
        scratchpad[EV_DS18B20_SCRATCHPAD_BYTES - 1U]) {
        return EV_ERR_CONTRACT;
    }
    return EV_OK;
}

ev_result_t ev_ds18b20_decode_measurement(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES],
                                           ev_ds18b20_measurement_t *out_measurement)
{
    int16_t raw;
    int32_t scaled;
    uint8_t resolution_bits = EV_DS18B20_DEFAULT_RESOLUTION_BITS;

    if ((scratchpad == NULL) || (out_measurement == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ev_ds18b20_validate_scratchpad(scratchpad) != EV_OK) {
        return EV_ERR_CONTRACT;
    }

    raw = (int16_t)(((uint16_t)scratchpad[1] << 8U) | (uint16_t)scratchpad[0]);
    switch (scratchpad[4] & EV_DS18B20_CFG_RESOLUTION_MASK) {
    case EV_DS18B20_CFG_9BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0007));
        resolution_bits = 9U;
        break;
    case EV_DS18B20_CFG_10BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0003));
        resolution_bits = 10U;
        break;
    case EV_DS18B20_CFG_11BIT:
        raw = (int16_t)(raw & (int16_t)(~0x0001));
        resolution_bits = 11U;
        break;
    case EV_DS18B20_CFG_12BIT:
    default:
        resolution_bits = 12U;
        break;
    }

    scaled = (int32_t)raw * 25;
    if (scaled >= 0) {
        scaled = (scaled + 2) / 4;
    } else {
        scaled = (scaled - 2) / 4;
    }

    out_measurement->centi_celsius = (int16_t)scaled;
    out_measurement->resolution_bits = resolution_bits;
    return EV_OK;
}

ev_result_t ev_ds18b20_decode_centi_celsius(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES],
                                             int16_t *out_centi_celsius)
{
    ev_ds18b20_measurement_t measurement = {0};
    ev_result_t rc;

    if (out_centi_celsius == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_ds18b20_decode_measurement(scratchpad, &measurement);
    if (rc != EV_OK) {
        return rc;
    }
    *out_centi_celsius = measurement.centi_celsius;
    return EV_OK;
}
