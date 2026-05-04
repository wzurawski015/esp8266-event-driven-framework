#include "ev/actor_instance.h"

#include "ev/actor_catalog.h"

#include <stddef.h>

ev_result_t ev_actor_instance_validate(const ev_actor_instance_descriptor_t *instance)
{
    if ((instance == NULL) || (instance->module == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_actor_id_is_valid(instance->actor_id)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (instance->module->actor_id != instance->actor_id) {
        return EV_ERR_CONTRACT;
    }
    if ((instance->handler_fn == NULL) && (instance->module->handler_fn == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if ((instance->actor_context_size > 0U) && (instance->actor_context == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    return EV_OK;
}
