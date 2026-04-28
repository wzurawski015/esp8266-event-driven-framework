#ifndef EV_PAYLOAD_KIND_H
#define EV_PAYLOAD_KIND_H

/**
 * @brief Payload transport policy associated with an event.
 */
typedef enum {
    EV_PAYLOAD_INLINE = 0,
    EV_PAYLOAD_COPY_FIXED = 1,
    EV_PAYLOAD_LEASE = 2,
    EV_PAYLOAD_STREAM_VIEW = 3
} ev_payload_kind_t;

/**
 * @brief Return a stable textual name for a payload kind.
 *
 * @param kind Payload kind enumerator.
 * @return Constant string describing the payload kind.
 */
const char *ev_payload_kind_name(ev_payload_kind_t kind);

#endif /* EV_PAYLOAD_KIND_H */
