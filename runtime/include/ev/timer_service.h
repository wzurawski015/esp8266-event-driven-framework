#ifndef EV_TIMER_SERVICE_H
#define EV_TIMER_SERVICE_H

#include <stddef.h>
#include <stdint.h>
#include "ev/actor_id.h"
#include "ev/event_id.h"
#include "ev/msg.h"
#include "ev/result.h"

#ifndef EV_TIMER_SERVICE_CAPACITY
#define EV_TIMER_SERVICE_CAPACITY 16U
#endif

typedef struct {
    uint16_t slot;
    uint16_t generation;
} ev_timer_token_t;

typedef ev_result_t (*ev_timer_publish_fn_t)(ev_actor_id_t target_actor, const ev_msg_t *msg, void *ctx);

typedef struct {
    uint8_t active;
    uint8_t periodic;
    uint16_t generation;
    uint32_t deadline_ms;
    uint32_t period_ms;
    ev_actor_id_t target_actor;
    ev_event_id_t event_id;
    uint32_t arg0;
} ev_timer_slot_t;

typedef struct {
    ev_timer_slot_t slots[EV_TIMER_SERVICE_CAPACITY];
    uint32_t scheduled;
    uint32_t cancelled;
    uint32_t published;
} ev_timer_service_t;

void ev_timer_service_init(ev_timer_service_t *svc);
ev_result_t ev_timer_schedule_oneshot(ev_timer_service_t *svc, uint32_t now_ms, uint32_t delay_ms, ev_actor_id_t target_actor, ev_event_id_t event_id, uint32_t arg0, ev_timer_token_t *out_token);
ev_result_t ev_timer_schedule_periodic(ev_timer_service_t *svc, uint32_t now_ms, uint32_t period_ms, ev_actor_id_t target_actor, ev_event_id_t event_id, uint32_t arg0, ev_timer_token_t *out_token);
ev_result_t ev_timer_cancel(ev_timer_service_t *svc, ev_timer_token_t token);
size_t ev_timer_publish_due(ev_timer_service_t *svc, uint32_t now_ms, ev_timer_publish_fn_t publish, void *ctx, size_t max_publish);
ev_result_t ev_timer_next_deadline_ms(const ev_timer_service_t *svc, uint32_t *out_deadline_ms);
size_t ev_timer_pending_count(const ev_timer_service_t *svc);
int ev_timer_is_due(uint32_t now_ms, uint32_t deadline_ms);

#endif
