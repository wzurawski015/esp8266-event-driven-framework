#ifndef EV_DS18B20_DRIVER_H
#define EV_DS18B20_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "ev/port_onewire.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_DS18B20_SCRATCHPAD_BYTES 9U
#define EV_DS18B20_ROM_BYTES 8U
#define EV_DS18B20_DEFAULT_RESOLUTION_BITS 12U

typedef struct ev_ds18b20_rom {
    uint8_t bytes[EV_DS18B20_ROM_BYTES];
} ev_ds18b20_rom_t;

typedef struct ev_ds18b20_measurement {
    int16_t centi_celsius;
    uint8_t resolution_bits;
} ev_ds18b20_measurement_t;

uint8_t ev_ds18b20_crc8(const uint8_t *data, size_t data_len);

uint16_t ev_ds18b20_conversion_time_ms(uint8_t resolution_bits);

ev_result_t ev_ds18b20_start_conversion_skip_rom(const ev_onewire_port_t *onewire_port);

ev_result_t ev_ds18b20_read_scratchpad_skip_rom(const ev_onewire_port_t *onewire_port,
                                                uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES]);

ev_result_t ev_ds18b20_validate_scratchpad(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES]);

ev_result_t ev_ds18b20_decode_measurement(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES],
                                           ev_ds18b20_measurement_t *out_measurement);

ev_result_t ev_ds18b20_decode_centi_celsius(const uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES],
                                             int16_t *out_centi_celsius);

#ifdef __cplusplus
}
#endif

#endif /* EV_DS18B20_DRIVER_H */
