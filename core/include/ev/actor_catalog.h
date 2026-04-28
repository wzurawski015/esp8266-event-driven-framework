#ifndef EV_ACTOR_CATALOG_H
#define EV_ACTOR_CATALOG_H

#include <stdbool.h>
#include <stddef.h>

#include "ev/actor_id.h"
#include "ev/execution_domain.h"
#include "ev/mailbox_kind.h"

/**
 * @brief Metadata associated with a declared actor.
 */
typedef struct {
    ev_actor_id_t id;
    const char *name;
    ev_execution_domain_t execution_domain;
    ev_mailbox_kind_t mailbox_kind;
    size_t drain_budget;
    const char *summary;
} ev_actor_meta_t;

/**
 * @brief Return the number of declared actors.
 *
 * @return Number of items in the actor catalog.
 */
size_t ev_actor_count(void);

/**
 * @brief Look up metadata for a declared actor.
 *
 * @param id Actor identifier.
 * @return Pointer to catalog entry or NULL if out of range.
 */
const ev_actor_meta_t *ev_actor_meta(ev_actor_id_t id);

/**
 * @brief Return a stable textual name for an actor identifier.
 *
 * @param id Actor identifier.
 * @return Constant string or NULL if out of range.
 */
const char *ev_actor_name(ev_actor_id_t id);

/**
 * @brief Return the default bounded-drain budget for one actor.
 *
 * @param id Actor identifier.
 * @return Configured bounded-drain budget or 0 when the actor is invalid.
 */
size_t ev_actor_default_drain_budget(ev_actor_id_t id);

/**
 * @brief Test whether an actor identifier is valid.
 *
 * @param id Actor identifier.
 * @return true when the identifier exists in the catalog.
 */
bool ev_actor_id_is_valid(ev_actor_id_t id);

#endif /* EV_ACTOR_CATALOG_H */
