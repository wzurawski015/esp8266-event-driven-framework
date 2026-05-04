#include "fake_log_port.h"

#include <string.h>

static ev_result_t fake_log_write(void *ctx,
                                  ev_log_level_t level,
                                  const char *tag,
                                  const char *message,
                                  size_t message_len)
{
    fake_log_port_t *fake = (fake_log_port_t *)ctx;

    (void)level;
    (void)tag;
    (void)message;
    (void)message_len;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->write_calls;
    if (fake->shared_sequence != NULL) {
        ++(*fake->shared_sequence);
        fake->write_order = *fake->shared_sequence;
    } else {
        ++fake->call_sequence;
        fake->write_order = fake->call_sequence;
    }
    return fake->next_write_result;
}


static ev_result_t fake_log_pending(void *ctx, uint32_t *out_pending_records)
{
    fake_log_port_t *fake = (fake_log_port_t *)ctx;

    if ((fake == NULL) || (out_pending_records == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->pending_calls;
    *out_pending_records = fake->pending_records;
    return fake->next_pending_result;
}

static ev_result_t fake_log_flush(void *ctx)
{
    fake_log_port_t *fake = (fake_log_port_t *)ctx;

    if (fake == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    ++fake->flush_calls;
    if (fake->shared_sequence != NULL) {
        ++(*fake->shared_sequence);
        fake->flush_order = *fake->shared_sequence;
    } else {
        ++fake->call_sequence;
        fake->flush_order = fake->call_sequence;
    }
    return fake->next_flush_result;
}

void fake_log_port_init(fake_log_port_t *fake)
{
    if (fake != NULL) {
        memset(fake, 0, sizeof(*fake));
        fake->next_write_result = EV_OK;
        fake->next_flush_result = EV_OK;
        fake->next_pending_result = EV_OK;
    }
}

void fake_log_port_bind(ev_log_port_t *out_port, fake_log_port_t *fake)
{
    if (out_port != NULL) {
        out_port->ctx = fake;
        out_port->write = fake_log_write;
        out_port->flush = fake_log_flush;
        out_port->pending = fake_log_pending;
    }
}
