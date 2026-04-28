#include "ev/timer_service.h"

#include <string.h>

static int ev_timer_token_valid(const ev_timer_service_t *svc, ev_timer_token_t token)
{
    return (svc != NULL) &&
           (token.slot < EV_TIMER_SERVICE_CAPACITY) &&
           (svc->slots[token.slot].active != 0U) &&
           (svc->slots[token.slot].generation == token.generation);
}

int ev_timer_is_due(uint32_t now_ms, uint32_t deadline_ms)
{
    return ((int32_t)(now_ms - deadline_ms) >= 0) ? 1 : 0;
}

void ev_timer_service_init(ev_timer_service_t *svc)
{
    if (svc != NULL) {
        (void)memset(svc, 0, sizeof(*svc));
    }
}

static ev_result_t ev_timer_schedule(ev_timer_service_t *svc, uint32_t now_ms, uint32_t delay_ms, ev_actor_id_t target_actor, ev_event_id_t event_id, uint32_t arg0, uint8_t periodic, ev_timer_token_t *out_token)
{
    size_t i;

    if ((svc == NULL) || (out_token == NULL) || (delay_ms == 0U)) {
        return EV_ERR_INVALID_ARG;
    }

    for (i = 0U; i < EV_TIMER_SERVICE_CAPACITY; ++i) {
        ev_timer_slot_t *slot = &svc->slots[i];
        if (slot->active == 0U) {
            slot->active = 1U;
            slot->periodic = periodic;
            slot->generation++;
            if (slot->generation == 0U) {
                slot->generation = 1U;
            }
            slot->deadline_ms = now_ms + delay_ms;
            slot->period_ms = delay_ms;
            slot->target_actor = target_actor;
            slot->event_id = event_id;
            slot->arg0 = arg0;
            out_token->slot = (uint16_t)i;
            out_token->generation = slot->generation;
            svc->scheduled++;
            return EV_OK;
        }
    }
    return EV_ERR_FULL;
}

ev_result_t ev_timer_schedule_oneshot(ev_timer_service_t *svc, uint32_t now_ms, uint32_t delay_ms, ev_actor_id_t target_actor, ev_event_id_t event_id, uint32_t arg0, ev_timer_token_t *out_token)
{
    return ev_timer_schedule(svc, now_ms, delay_ms, target_actor, event_id, arg0, 0U, out_token);
}

ev_result_t ev_timer_schedule_periodic(ev_timer_service_t *svc, uint32_t now_ms, uint32_t period_ms, ev_actor_id_t target_actor, ev_event_id_t event_id, uint32_t arg0, ev_timer_token_t *out_token)
{
    return ev_timer_schedule(svc, now_ms, period_ms, target_actor, event_id, arg0, 1U, out_token);
}

ev_result_t ev_timer_cancel(ev_timer_service_t *svc, ev_timer_token_t token)
{
    if (ev_timer_token_valid(svc, token) == 0) {
        return EV_ERR_NOT_FOUND;
    }
    svc->slots[token.slot].active = 0U;
    svc->cancelled++;
    return EV_OK;
}

size_t ev_timer_publish_due(ev_timer_service_t *svc, uint32_t now_ms, ev_timer_publish_fn_t publish, void *ctx, size_t max_publish)
{
    size_t i;
    size_t published = 0U;

    if ((svc == NULL) || (publish == NULL) || (max_publish == 0U)) {
        return 0U;
    }

    for (i = 0U; (i < EV_TIMER_SERVICE_CAPACITY) && (published < max_publish); ++i) {
        ev_timer_slot_t *slot = &svc->slots[i];
        if ((slot->active != 0U) && (ev_timer_is_due(now_ms, slot->deadline_ms) != 0)) {
            ev_msg_t msg = EV_MSG_INITIALIZER;
            if (ev_msg_init_send(&msg, slot->event_id, ACT_RUNTIME, slot->target_actor) == EV_OK) {
                uint32_t payload_arg = slot->arg0;
                (void)ev_msg_set_inline_payload(&msg, &payload_arg, sizeof(payload_arg));
                if (publish(slot->target_actor, &msg, ctx) == EV_OK) {
                    published++;
                    svc->published++;
                }
            }
            if (slot->periodic != 0U) {
                slot->deadline_ms += slot->period_ms;
            } else {
                slot->active = 0U;
            }
        }
    }

    return published;
}

ev_result_t ev_timer_next_deadline_ms(const ev_timer_service_t *svc, uint32_t *out_deadline_ms)
{
    size_t i;
    uint8_t found = 0U;
    uint32_t best = 0U;

    if ((svc == NULL) || (out_deadline_ms == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    for (i = 0U; i < EV_TIMER_SERVICE_CAPACITY; ++i) {
        const ev_timer_slot_t *slot = &svc->slots[i];
        if (slot->active != 0U) {
            if ((found == 0U) || ((int32_t)(slot->deadline_ms - best) < 0)) {
                best = slot->deadline_ms;
                found = 1U;
            }
        }
    }

    if (found == 0U) {
        return EV_ERR_NOT_FOUND;
    }
    *out_deadline_ms = best;
    return EV_OK;
}

size_t ev_timer_pending_count(const ev_timer_service_t *svc)
{
    size_t i;
    size_t n = 0U;

    if (svc == NULL) {
        return 0U;
    }
    for (i = 0U; i < EV_TIMER_SERVICE_CAPACITY; ++i) {
        if (svc->slots[i].active != 0U) {
            n++;
        }
    }
    return n;
}
