#ifndef EV_MAILBOX_KIND_H
#define EV_MAILBOX_KIND_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Supported mailbox semantics for actor inboxes.
 */
typedef enum {
    EV_MAILBOX_FIFO_8 = 0,
    EV_MAILBOX_FIFO_16 = 1,
    EV_MAILBOX_MAILBOX_1 = 2,
    EV_MAILBOX_LOSSY_RING_8 = 3,
    EV_MAILBOX_COALESCED_FLAG = 4
} ev_mailbox_kind_t;

/**
 * @brief Return a stable textual name for a mailbox kind.
 *
 * @param kind Mailbox kind enumerator.
 * @return Constant string describing the mailbox kind.
 */
const char *ev_mailbox_kind_name(ev_mailbox_kind_t kind);

/**
 * @brief Return the nominal queue capacity for a mailbox kind.
 *
 * @param kind Mailbox kind enumerator.
 * @return Queue capacity in message slots, or zero for an unknown kind.
 */
size_t ev_mailbox_kind_capacity(ev_mailbox_kind_t kind);

/**
 * @brief Test whether a mailbox kind may discard messages by design.
 *
 * @param kind Mailbox kind enumerator.
 * @return true when the mailbox kind is intentionally lossy.
 */
bool ev_mailbox_kind_is_lossy(ev_mailbox_kind_t kind);

/**
 * @brief Test whether a mailbox kind coalesces repeated work.
 *
 * @param kind Mailbox kind enumerator.
 * @return true when the mailbox kind performs coalescing.
 */
bool ev_mailbox_kind_is_coalescing(ev_mailbox_kind_t kind);

#endif /* EV_MAILBOX_KIND_H */
