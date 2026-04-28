#include "ev/msg.h"

#include <string.h>

#include "ev/actor_catalog.h"
#include "ev/dispose.h"

static bool ev_msg_has_cookie(const ev_msg_t *msg)
{
    return (msg != NULL) && (msg->cookie == EV_MSG_COOKIE);
}

static bool ev_payload_kind_allows_inline(ev_payload_kind_t kind)
{
    return (kind == EV_PAYLOAD_INLINE) || (kind == EV_PAYLOAD_COPY_FIXED);
}

static bool ev_payload_kind_allows_external(ev_payload_kind_t kind)
{
    return (kind == EV_PAYLOAD_LEASE) || (kind == EV_PAYLOAD_STREAM_VIEW);
}

static ev_result_t ev_msg_release_attached_payload(ev_msg_t *msg)
{
    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }

    if ((msg->storage == EV_MSG_STORAGE_EXTERNAL) && (msg->payload_size > 0U) &&
        (msg->payload.external.release_fn != NULL)) {
        msg->payload.external.release_fn(
            msg->payload.external.lifecycle_ctx,
            msg->payload.external.data,
            msg->payload_size);
    }

    memset(&msg->payload, 0, sizeof(msg->payload));
    msg->payload_size = 0U;
    msg->storage = EV_MSG_STORAGE_NONE;
    return EV_OK;
}

void ev_msg_reset(ev_msg_t *msg)
{
    if (msg != NULL) {
        memset(msg, 0, sizeof(*msg));
        msg->cookie = EV_MSG_COOKIE;
    }
}

ev_result_t ev_msg_init_publish(ev_msg_t *msg, ev_event_id_t event_id, ev_actor_id_t source_actor)
{
    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_event_id_is_valid(event_id) || !ev_actor_id_is_valid(source_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    ev_msg_reset(msg);
    msg->event_id = event_id;
    msg->source_actor = source_actor;
    msg->target_actor = EV_ACTOR_NONE;
    return EV_OK;
}

ev_result_t ev_msg_init_send(
    ev_msg_t *msg,
    ev_event_id_t event_id,
    ev_actor_id_t source_actor,
    ev_actor_id_t target_actor)
{
    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if ((target_actor == EV_ACTOR_NONE) || !ev_event_id_is_valid(event_id) || !ev_actor_id_is_valid(source_actor) ||
        !ev_actor_id_is_valid(target_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }

    ev_msg_reset(msg);
    msg->event_id = event_id;
    msg->source_actor = source_actor;
    msg->target_actor = target_actor;
    return EV_OK;
}

ev_payload_kind_t ev_msg_payload_kind(const ev_msg_t *msg)
{
    const ev_event_meta_t *meta;

    if (msg == NULL) {
        return EV_PAYLOAD_INLINE;
    }

    meta = ev_event_meta(msg->event_id);
    return (meta != NULL) ? meta->payload_kind : EV_PAYLOAD_INLINE;
}

ev_result_t ev_msg_set_inline_payload(ev_msg_t *msg, const void *data, size_t size)
{
    const ev_event_meta_t *meta;
    ev_result_t rc;

    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_msg_has_cookie(msg)) {
        return EV_ERR_STATE;
    }
    if (ev_msg_is_disposed(msg)) {
        return EV_ERR_STATE;
    }
    if ((size > 0U) && (data == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (size > EV_MSG_INLINE_CAPACITY) {
        return EV_ERR_OUT_OF_RANGE;
    }

    meta = ev_event_meta(msg->event_id);
    if (meta == NULL) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (!ev_payload_kind_allows_inline(meta->payload_kind)) {
        return EV_ERR_CONTRACT;
    }

    rc = ev_msg_release_attached_payload(msg);
    if (rc != EV_OK) {
        return rc;
    }

    memset(&msg->payload, 0, sizeof(msg->payload));
    if (size > 0U) {
        memcpy(msg->payload.inline_bytes, data, size);
        msg->storage = EV_MSG_STORAGE_INLINE;
    } else {
        msg->storage = EV_MSG_STORAGE_NONE;
    }
    msg->payload_size = size;
    return EV_OK;
}

ev_result_t ev_msg_set_external_payload(
    ev_msg_t *msg,
    const void *data,
    size_t size,
    ev_msg_retain_fn_t retain_fn,
    ev_msg_release_fn_t release_fn,
    void *lifecycle_ctx)
{
    const ev_event_meta_t *meta;
    ev_result_t rc;

    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_msg_has_cookie(msg)) {
        return EV_ERR_STATE;
    }
    if (ev_msg_is_disposed(msg)) {
        return EV_ERR_STATE;
    }
    if ((size > 0U) && (data == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    meta = ev_event_meta(msg->event_id);
    if (meta == NULL) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if (!ev_payload_kind_allows_external(meta->payload_kind)) {
        return EV_ERR_CONTRACT;
    }
    if ((meta->payload_kind == EV_PAYLOAD_LEASE) && (size > 0U) &&
        ((retain_fn == NULL) || (release_fn == NULL))) {
        return EV_ERR_CONTRACT;
    }

    rc = ev_msg_release_attached_payload(msg);
    if (rc != EV_OK) {
        return rc;
    }

    memset(&msg->payload, 0, sizeof(msg->payload));
    if (size > 0U) {
        msg->storage = EV_MSG_STORAGE_EXTERNAL;
        msg->payload.external.data = data;
        msg->payload.external.size = size;
        msg->payload.external.retain_fn = retain_fn;
        msg->payload.external.release_fn = release_fn;
        msg->payload.external.lifecycle_ctx = lifecycle_ctx;
    } else {
        msg->storage = EV_MSG_STORAGE_NONE;
    }
    msg->payload_size = size;
    return EV_OK;
}

ev_result_t ev_msg_retain(const ev_msg_t *msg)
{
    const ev_event_meta_t *meta;

    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_msg_has_cookie(msg)) {
        return EV_ERR_STATE;
    }

    meta = ev_event_meta(msg->event_id);
    if (meta == NULL) {
        return EV_ERR_OUT_OF_RANGE;
    }

    if ((msg->storage != EV_MSG_STORAGE_EXTERNAL) || (msg->payload_size == 0U)) {
        return EV_OK;
    }

    switch (meta->payload_kind) {
    case EV_PAYLOAD_LEASE:
        if (msg->payload.external.retain_fn == NULL) {
            return EV_ERR_UNSUPPORTED;
        }
        return msg->payload.external.retain_fn(
            msg->payload.external.lifecycle_ctx,
            msg->payload.external.data,
            msg->payload_size);

    case EV_PAYLOAD_STREAM_VIEW:
        return EV_ERR_UNSUPPORTED;

    default:
        return EV_ERR_CONTRACT;
    }
}

ev_result_t ev_msg_validate(const ev_msg_t *msg)
{
    const ev_event_meta_t *meta;

    if (msg == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_msg_has_cookie(msg)) {
        return EV_ERR_STATE;
    }
    if (ev_msg_is_disposed(msg)) {
        return EV_ERR_STATE;
    }
    if (!ev_event_id_is_valid(msg->event_id) || !ev_actor_id_is_valid(msg->source_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if ((msg->target_actor != EV_ACTOR_NONE) && !ev_actor_id_is_valid(msg->target_actor)) {
        return EV_ERR_OUT_OF_RANGE;
    }
    if ((msg->storage == EV_MSG_STORAGE_NONE) && (msg->payload_size != 0U)) {
        return EV_ERR_CONTRACT;
    }
    if ((msg->storage != EV_MSG_STORAGE_NONE) && (msg->payload_size == 0U)) {
        return EV_ERR_CONTRACT;
    }

    meta = ev_event_meta(msg->event_id);
    if (meta == NULL) {
        return EV_ERR_OUT_OF_RANGE;
    }

    switch (meta->payload_kind) {
    case EV_PAYLOAD_INLINE:
    case EV_PAYLOAD_COPY_FIXED:
        if (msg->storage == EV_MSG_STORAGE_EXTERNAL) {
            return EV_ERR_CONTRACT;
        }
        if (msg->payload_size > EV_MSG_INLINE_CAPACITY) {
            return EV_ERR_OUT_OF_RANGE;
        }
        break;

    case EV_PAYLOAD_LEASE:
        if (msg->storage == EV_MSG_STORAGE_INLINE) {
            return EV_ERR_CONTRACT;
        }
        if ((msg->storage == EV_MSG_STORAGE_EXTERNAL) && (msg->payload_size > 0U) &&
            ((msg->payload.external.data == NULL) || (msg->payload.external.size != msg->payload_size) ||
             (msg->payload.external.retain_fn == NULL) || (msg->payload.external.release_fn == NULL))) {
            return EV_ERR_CONTRACT;
        }
        break;

    case EV_PAYLOAD_STREAM_VIEW:
        if (msg->storage == EV_MSG_STORAGE_INLINE) {
            return EV_ERR_CONTRACT;
        }
        if ((msg->storage == EV_MSG_STORAGE_EXTERNAL) && (msg->payload_size > 0U) &&
            ((msg->payload.external.data == NULL) || (msg->payload.external.size != msg->payload_size))) {
            return EV_ERR_CONTRACT;
        }
        break;

    default:
        return EV_ERR_CONTRACT;
    }

    return EV_OK;
}

bool ev_msg_is_disposed(const ev_msg_t *msg)
{
    return ev_msg_has_cookie(msg) && ((msg->flags & EV_MSG_F_DISPOSED) != 0U);
}

const void *ev_msg_payload_data(const ev_msg_t *msg)
{
    if (msg == NULL) {
        return NULL;
    }

    switch (msg->storage) {
    case EV_MSG_STORAGE_INLINE:
        return msg->payload.inline_bytes;
    case EV_MSG_STORAGE_EXTERNAL:
        return msg->payload.external.data;
    case EV_MSG_STORAGE_NONE:
    default:
        return NULL;
    }
}

size_t ev_msg_payload_size(const ev_msg_t *msg)
{
    return (msg != NULL) ? msg->payload_size : 0U;
}
