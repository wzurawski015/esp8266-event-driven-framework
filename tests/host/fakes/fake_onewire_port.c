#include "fake_onewire_port.h"

#include <string.h>

static ev_onewire_status_t fake_onewire_reset(void *ctx)
{
    fake_onewire_port_t *fake = (fake_onewire_port_t *)ctx;

    if (fake == NULL) {
        return EV_ONEWIRE_ERR_BUS;
    }

    ++fake->reset_calls;
    fake->read_index = 0U;
    if (!fake->present) {
        return EV_ONEWIRE_ERR_NO_DEVICE;
    }
    return fake->reset_status;
}

static ev_onewire_status_t fake_onewire_write_byte(void *ctx, uint8_t value)
{
    fake_onewire_port_t *fake = (fake_onewire_port_t *)ctx;

    if (fake == NULL) {
        return EV_ONEWIRE_ERR_BUS;
    }

    ++fake->write_calls;
    if (fake->last_written_len < (sizeof(fake->last_written) / sizeof(fake->last_written[0]))) {
        fake->last_written[fake->last_written_len++] = value;
    }
    return fake->write_status;
}

static ev_onewire_status_t fake_onewire_read_byte(void *ctx, uint8_t *out_value)
{
    fake_onewire_port_t *fake = (fake_onewire_port_t *)ctx;

    if ((fake == NULL) || (out_value == NULL)) {
        return EV_ONEWIRE_ERR_BUS;
    }

    ++fake->read_calls;
    if ((fake->read_status != EV_ONEWIRE_OK) || (fake->read_index >= fake->read_len)) {
        return (fake->read_status != EV_ONEWIRE_OK) ? fake->read_status : EV_ONEWIRE_ERR_BUS;
    }

    *out_value = fake->read_bytes[fake->read_index++];
    return EV_ONEWIRE_OK;
}

void fake_onewire_port_init(fake_onewire_port_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
        fake->present = true;
        fake->reset_status = EV_ONEWIRE_OK;
        fake->write_status = EV_ONEWIRE_OK;
        fake->read_status = EV_ONEWIRE_OK;
    }
}

void fake_onewire_port_bind(ev_onewire_port_t *out_port, fake_onewire_port_t *fake)
{
    if (out_port != NULL) {
        memset(out_port, 0, sizeof(*out_port));
        out_port->ctx = fake;
        out_port->reset = fake_onewire_reset;
        out_port->write_byte = fake_onewire_write_byte;
        out_port->read_byte = fake_onewire_read_byte;
    }
}

void fake_onewire_port_seed_read_bytes(fake_onewire_port_t *fake, const uint8_t *src, size_t len)
{
    if ((fake != NULL) && (src != NULL) && (len <= sizeof(fake->read_bytes))) {
        memcpy(fake->read_bytes, src, len);
        fake->read_len = len;
        fake->read_index = 0U;
    }
}
