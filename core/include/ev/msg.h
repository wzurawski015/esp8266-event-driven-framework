#ifndef EV_MSG_H
#define EV_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/actor_id.h"
#include "ev/event_catalog.h"
#include "ev/msg_flags.h"
#include "ev/payload_kind.h"
#include "ev/result.h"

#define EV_MSG_INLINE_CAPACITY 24U
#define EV_MSG_COOKIE 0x45564D53u
#define EV_MSG_INITIALIZER {0}

/**
 * @brief Physical storage used by the runtime envelope.
 */
typedef enum {
    EV_MSG_STORAGE_NONE = 0,
    EV_MSG_STORAGE_INLINE = 1,
    EV_MSG_STORAGE_EXTERNAL = 2
} ev_msg_storage_t;

/**
 * @brief Retain callback for externally owned payloads.
 *
 * The callback must acquire one additional ownership share for the referenced
 * payload. It is used when the runtime needs to duplicate transport ownership,
 * for example while enqueueing the same leased payload into a mailbox.
 *
 * @param lifecycle_ctx Caller-provided lifecycle context.
 * @param payload Payload pointer previously attached to the message.
 * @param payload_size Payload size in bytes.
 * @return EV_OK on success or an error code.
 */
typedef ev_result_t (*ev_msg_retain_fn_t)(void *lifecycle_ctx, const void *payload, size_t payload_size);

/**
 * @brief Release callback for externally owned payloads.
 *
 * @param lifecycle_ctx Caller-provided lifecycle context.
 * @param payload Payload pointer previously attached to the message.
 * @param payload_size Payload size in bytes.
 */
typedef void (*ev_msg_release_fn_t)(void *lifecycle_ctx, const void *payload, size_t payload_size);

/**
 * @brief Descriptor for externally owned payload storage.
 */
typedef struct {
    const void *data;
    size_t size;
    ev_msg_retain_fn_t retain_fn;
    ev_msg_release_fn_t release_fn;
    void *lifecycle_ctx;
} ev_msg_external_payload_t;

/**
 * @brief Canonical runtime transport envelope.
 *
 * The semantic payload kind is defined by the event catalog. The message stores
 * only the transport descriptor required to access, retain, and dispose the payload.
 */
typedef struct {
    uint32_t cookie;
    ev_event_id_t event_id;
    ev_actor_id_t source_actor;
    ev_actor_id_t target_actor;
    uint32_t flags;
    ev_msg_storage_t storage;
    size_t payload_size;
    union {
        unsigned char inline_bytes[EV_MSG_INLINE_CAPACITY];
        ev_msg_external_payload_t external;
    } payload;
} ev_msg_t;

/**
 * @brief Reset a message to a zero-initialized state.
 *
 * This function performs a raw reset only. Callers must ensure that any owned
 * external payload has already been released or moved away before invoking it.
 * The reset also stamps the message with a contract cookie so that later reuse
 * can distinguish a known envelope from indeterminate stack bytes.
 *
 * @param msg Message to reset.
 */
void ev_msg_reset(ev_msg_t *msg);

/**
 * @brief Initialize a message for publish delivery.
 *
 * Initialization performs a blind overwrite and is safe for arbitrary stack
 * storage. It does not release any previously attached payload. Call
 * ev_msg_dispose() explicitly before reusing a populated message.
 *
 * @param msg Message to initialize.
 * @param event_id Declared event identifier.
 * @param source_actor Declared sender actor.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_init_publish(ev_msg_t *msg, ev_event_id_t event_id, ev_actor_id_t source_actor);

/**
 * @brief Initialize a message for direct send delivery.
 *
 * Initialization performs a blind overwrite and is safe for arbitrary stack
 * storage. It does not release any previously attached payload. Call
 * ev_msg_dispose() explicitly before reusing a populated message.
 *
 * @param msg Message to initialize.
 * @param event_id Declared event identifier.
 * @param source_actor Declared sender actor.
 * @param target_actor Declared receiver actor.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_init_send(
    ev_msg_t *msg,
    ev_event_id_t event_id,
    ev_actor_id_t source_actor,
    ev_actor_id_t target_actor);

/**
 * @brief Attach inline payload bytes to a message.
 *
 * This is valid only for events whose catalog payload kind is INLINE or
 * COPY_FIXED. Replacing an already attached payload first releases the previous
 * payload when needed.
 *
 * @param msg Message to modify.
 * @param data Source bytes. May be NULL only when size is zero.
 * @param size Number of bytes to copy.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_set_inline_payload(ev_msg_t *msg, const void *data, size_t size);

/**
 * @brief Attach externally owned payload storage to a message.
 *
 * This is valid only for events whose catalog payload kind is LEASE or
 * STREAM_VIEW. Replacing an already attached payload first releases the
 * previous payload when needed.
 *
 * For LEASE payloads, both retain_fn and release_fn are required for any
 * non-empty payload so that queueing, fan-out, and disposal all share one
 * explicit ownership contract from the moment the payload is attached.
 *
 * @param msg Message to modify.
 * @param data External payload pointer. May be NULL only when size is zero.
 * @param size Payload size in bytes.
 * @param retain_fn Retain callback for duplicate transport ownership. Required for
 *        non-empty LEASE payloads.
 * @param release_fn Release callback. Required for non-empty LEASE payloads.
 * @param lifecycle_ctx Caller-provided context for retain and release callbacks.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_set_external_payload(
    ev_msg_t *msg,
    const void *data,
    size_t size,
    ev_msg_retain_fn_t retain_fn,
    ev_msg_release_fn_t release_fn,
    void *lifecycle_ctx);

/**
 * @brief Acquire one additional ownership share for the current payload.
 *
 * This is primarily used by delivery code that needs to retain a lease-backed
 * payload across mailbox copies. Inline and empty payloads are a no-op.
 * Stream views remain unsupported for mailbox ownership duplication.
 *
 * @param msg Message to retain.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_retain(const ev_msg_t *msg);

/**
 * @brief Validate the internal consistency of a runtime message.
 *
 * @param msg Message to validate.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_msg_validate(const ev_msg_t *msg);

/**
 * @brief Determine whether a message has already been disposed.
 *
 * @param msg Message to inspect.
 * @return true if the message has been disposed, otherwise false.
 */
bool ev_msg_is_disposed(const ev_msg_t *msg);

/**
 * @brief Return the catalog payload kind for a message.
 *
 * @param msg Message to inspect.
 * @return Catalog payload kind or EV_PAYLOAD_INLINE if unavailable.
 */
ev_payload_kind_t ev_msg_payload_kind(const ev_msg_t *msg);

/**
 * @brief Return a pointer to the current payload bytes, if any.
 *
 * @param msg Message to inspect.
 * @return Payload pointer or NULL when no payload is attached.
 */
const void *ev_msg_payload_data(const ev_msg_t *msg);

/**
 * @brief Return the current payload size in bytes.
 *
 * @param msg Message to inspect.
 * @return Payload size in bytes.
 */
size_t ev_msg_payload_size(const ev_msg_t *msg);

#endif /* EV_MSG_H */
