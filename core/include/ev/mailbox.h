#ifndef EV_MAILBOX_H
#define EV_MAILBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/mailbox_kind.h"
#include "ev/msg.h"
#include "ev/result.h"

/**
 * @brief Mailbox diagnostics exposed by the contract-stage runtime.
 */
typedef struct {
    uint32_t posted;
    uint32_t dropped;
    uint32_t replaced;
    uint32_t coalesced;
    uint32_t popped;
    uint32_t rejected;
    size_t high_watermark;
} ev_mailbox_stats_t;

/**
 * @brief Queue state for one actor mailbox.
 *
 * Storage is provided by the caller and remains owned by the caller.
 * The mailbox stores runtime envelopes by value. When a message carries a
 * retainable LEASE payload, enqueue acquires one additional ownership share for
 * the queued copy. Unsupported external payload kinds are rejected.
 */
typedef struct {
    ev_mailbox_kind_t kind;
    ev_msg_t *storage;
    size_t storage_count;
    size_t storage_mask;
    size_t head;
    size_t tail;
    size_t count;
    ev_mailbox_stats_t stats;
} ev_mailbox_t;

/**
 * @brief Initialize a mailbox over caller-provided storage.
 *
 * @param mailbox Mailbox to initialize.
 * @param kind Mailbox semantics.
 * @param storage Caller-owned message storage.
 * @param storage_count Number of storage slots.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_mailbox_init(
    ev_mailbox_t *mailbox,
    ev_mailbox_kind_t kind,
    ev_msg_t *storage,
    size_t storage_count);

/**
 * @brief Reset mailbox queue state and diagnostics.
 *
 * Any retained payloads currently owned by queued messages are released before
 * the queue state is reset.
 *
 * @param mailbox Mailbox to reset.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_mailbox_reset(ev_mailbox_t *mailbox);

/**
 * @brief Post one message to a mailbox.
 *
 * The mailbox takes a by-value snapshot of the runtime envelope. For LEASE
 * payloads this acquires one additional retained ownership share for the queued
 * copy. Unsupported external payload kinds are rejected.
 *
 * @param mailbox Mailbox receiving the message.
 * @param msg Message to enqueue.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_mailbox_push(ev_mailbox_t *mailbox, const ev_msg_t *msg);

/**
 * @brief Pop one message from a mailbox.
 *
 * Ownership of any queued payload moves into @p out and must be disposed by the
 * caller once processing completes.
 *
 * @param mailbox Mailbox to drain.
 * @param out Output receiving the next message.
 * @return EV_OK on success, EV_ERR_EMPTY when no message is pending, or an error code.
 */
ev_result_t ev_mailbox_pop(ev_mailbox_t *mailbox, ev_msg_t *out);

/**
 * @brief Return the number of pending messages.
 *
 * @param mailbox Mailbox to inspect.
 * @return Pending message count.
 */
size_t ev_mailbox_count(const ev_mailbox_t *mailbox);

/**
 * @brief Return the queue capacity implied by the mailbox kind.
 *
 * @param mailbox Mailbox to inspect.
 * @return Queue capacity in slots.
 */
size_t ev_mailbox_capacity(const ev_mailbox_t *mailbox);

/**
 * @brief Test whether the mailbox has no pending messages.
 *
 * @param mailbox Mailbox to inspect.
 * @return true when empty.
 */
bool ev_mailbox_is_empty(const ev_mailbox_t *mailbox);

/**
 * @brief Test whether the mailbox is currently full.
 *
 * @param mailbox Mailbox to inspect.
 * @return true when full.
 */
bool ev_mailbox_is_full(const ev_mailbox_t *mailbox);

/**
 * @brief Return a stable pointer to mailbox diagnostics.
 *
 * @param mailbox Mailbox to inspect.
 * @return Pointer to diagnostics or NULL when mailbox is NULL.
 */
const ev_mailbox_stats_t *ev_mailbox_stats(const ev_mailbox_t *mailbox);

#endif /* EV_MAILBOX_H */
