#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ev/ds18b20_driver.h"
#include "fakes/fake_onewire_port.h"

static void seed_valid_scratchpad(uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES])
{
    memset(scratchpad, 0, EV_DS18B20_SCRATCHPAD_BYTES);
    scratchpad[0] = 0x91U; /* 25.0625 C raw LSB */
    scratchpad[1] = 0x01U;
    scratchpad[2] = 0x4BU;
    scratchpad[3] = 0x46U;
    scratchpad[4] = 0x7FU; /* 12-bit resolution */
    scratchpad[5] = 0xFFU;
    scratchpad[6] = 0x0CU;
    scratchpad[7] = 0x10U;
    scratchpad[8] = ev_ds18b20_crc8(scratchpad, EV_DS18B20_SCRATCHPAD_BYTES - 1U);
}

int main(void)
{
    fake_onewire_port_t fake;
    ev_onewire_port_t port;
    uint8_t scratchpad[EV_DS18B20_SCRATCHPAD_BYTES];
    ev_ds18b20_measurement_t measurement = {0};
    int16_t centi = 0;

    fake_onewire_port_init(&fake);
    fake_onewire_port_bind(&port, &fake);
    seed_valid_scratchpad(scratchpad);

    assert(ev_ds18b20_conversion_time_ms(9U) == 94U);
    assert(ev_ds18b20_conversion_time_ms(10U) == 188U);
    assert(ev_ds18b20_conversion_time_ms(11U) == 375U);
    assert(ev_ds18b20_conversion_time_ms(12U) == 750U);
    assert(ev_ds18b20_validate_scratchpad(scratchpad) == EV_OK);
    assert(ev_ds18b20_decode_measurement(scratchpad, &measurement) == EV_OK);
    assert(measurement.centi_celsius == 2506);
    assert(measurement.resolution_bits == 12U);
    assert(ev_ds18b20_decode_centi_celsius(scratchpad, &centi) == EV_OK);
    assert(centi == 2506);

    scratchpad[0] ^= 0x01U;
    assert(ev_ds18b20_validate_scratchpad(scratchpad) == EV_ERR_CONTRACT);
    assert(ev_ds18b20_decode_measurement(scratchpad, &measurement) == EV_ERR_CONTRACT);
    seed_valid_scratchpad(scratchpad);

    assert(ev_ds18b20_start_conversion_skip_rom(&port) == EV_OK);
    assert(fake.reset_calls == 1U);
    assert(fake.write_calls == 2U);
    assert(fake.last_written[0] == 0xCCU);
    assert(fake.last_written[1] == 0x44U);

    fake_onewire_port_seed_read_bytes(&fake, scratchpad, sizeof(scratchpad));
    assert(ev_ds18b20_read_scratchpad_skip_rom(&port, scratchpad) == EV_OK);
    assert(fake.read_calls == EV_DS18B20_SCRATCHPAD_BYTES);
    assert(fake.last_written[fake.last_written_len - 2U] == 0xCCU);
    assert(fake.last_written[fake.last_written_len - 1U] == 0xBEU);

    fake.present = false;
    assert(ev_ds18b20_start_conversion_skip_rom(&port) == EV_ERR_NOT_FOUND);
    assert(ev_ds18b20_read_scratchpad_skip_rom(&port, scratchpad) == EV_ERR_NOT_FOUND);

    assert(ev_ds18b20_start_conversion_skip_rom(NULL) == EV_ERR_INVALID_ARG);
    assert(ev_ds18b20_read_scratchpad_skip_rom(&port, NULL) == EV_ERR_INVALID_ARG);
    assert(ev_ds18b20_decode_centi_celsius(scratchpad, NULL) == EV_ERR_INVALID_ARG);

    return 0;
}
