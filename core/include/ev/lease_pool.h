#ifndef EV_LEASE_POOL_H
#define EV_LEASE_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/msg.h"
#include "ev/result.h"

typedef struct ev_lease_pool ev_lease_pool_t;
typedef struct ev_lease_slot ev_lease_slot_t;

/**
 * @brief Handle representing one caller-owned lease share.
 */
typedef struct {
    ev_lease_slot_t *slot;
    uint32_t generation;
} ev_lease_handle_t;

/**
 * @brief Public diagnostics for the deterministic lease pool.
 */
typedef struct {
    uint32_t acquires;
    uint32_t failed_acquires;
    uint32_t retains;
    uint32_t releases;
    uint32_t stale_handles;
    size_t in_use;
    size_t high_watermark;
} ev_lease_pool_stats_t;

/**
 * @brief Metadata associated with one pool slot.
 *
 * Caller allocates the slot array statically and passes it to ev_lease_pool_init().
 */
struct ev_lease_slot {
    ev_lease_pool_t *pool;
    size_t slot_index;
    uint32_t generation;
    uint32_t refcount;
    size_t payload_size;
    bool in_use;
};

/**
 * @brief Deterministic fixed-slot lease pool.
 *
 * All backing storage is caller-provided. Each slot owns exactly one fixed-size
 * payload region inside @p storage.
 */
struct ev_lease_pool {
    ev_lease_slot_t *slots;
    unsigned char *storage;
    size_t slot_count;
    size_t slot_size;
    ev_lease_pool_stats_t stats;
};

/**
 * @brief Initialize a deterministic fixed-slot lease pool.
 *
 * @param pool Pool object to initialize.
 * @param slots Caller-owned slot metadata array.
 * @param storage Caller-owned payload backing storage.
 * @param slot_count Number of pool slots.
 * @param slot_size Capacity in bytes of each slot.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_lease_pool_init(
    ev_lease_pool_t *pool,
    ev_lease_slot_t *slots,
    unsigned char *storage,
    size_t slot_count,
    size_t slot_size);

/**
 * @brief Acquire one free lease slot.
 *
 * The returned handle owns one reference. The caller must eventually release
 * that handle or move its ownership into another contract.
 *
 * @param pool Pool to acquire from.
 * @param payload_size Requested payload size in bytes.
 * @param out_handle Returned lease handle.
 * @param out_data Returned writable payload pointer.
 * @return EV_OK on success, EV_ERR_FULL when exhausted, or an error code.
 */
ev_result_t ev_lease_pool_acquire(
    ev_lease_pool_t *pool,
    size_t payload_size,
    ev_lease_handle_t *out_handle,
    void **out_data);

/**
 * @brief Test whether a handle still refers to a live lease slot.
 *
 * @param handle Handle to inspect.
 * @return true when the handle is valid and currently owned.
 */
bool ev_lease_handle_is_valid(const ev_lease_handle_t *handle);

/**
 * @brief Return a writable pointer to the payload bytes for a live handle.
 *
 * @param handle Handle to inspect.
 * @return Payload pointer or NULL when the handle is invalid.
 */
void *ev_lease_handle_data(const ev_lease_handle_t *handle);

/**
 * @brief Return the payload size associated with a live handle.
 *
 * @param handle Handle to inspect.
 * @return Payload size in bytes, or zero when invalid.
 */
size_t ev_lease_handle_size(const ev_lease_handle_t *handle);

/**
 * @brief Return the current reference count for a live handle.
 *
 * @param handle Handle to inspect.
 * @return Reference count, or zero when invalid.
 */
uint32_t ev_lease_handle_refcount(const ev_lease_handle_t *handle);

/**
 * @brief Acquire one additional reference for a live handle.
 *
 * @param handle Handle to retain.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_lease_pool_retain(const ev_lease_handle_t *handle);

/**
 * @brief Release one reference owned through a live handle.
 *
 * @param handle Handle to release.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_lease_pool_release(const ev_lease_handle_t *handle);

/**
 * @brief Return pool diagnostics.
 *
 * @param pool Pool to inspect.
 * @return Constant pointer to pool statistics or NULL when @p pool is NULL.
 */
const ev_lease_pool_stats_t *ev_lease_pool_stats(const ev_lease_pool_t *pool);

/**
 * @brief Attach one pool-backed lease payload to a runtime message.
 *
 * This helper retains one additional pool reference for the message envelope.
 * The caller remains responsible for releasing its original handle later.
 * The message must eventually be disposed.
 *
 * @param msg Message to modify.
 * @param handle Live handle referencing the payload to attach.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_lease_pool_attach_msg(ev_msg_t *msg, const ev_lease_handle_t *handle);

#endif /* EV_LEASE_POOL_H */
