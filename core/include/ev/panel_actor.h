#ifndef EV_PANEL_ACTOR_H
#define EV_PANEL_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_PANEL_BUTTON_COUNT 4U
#define EV_BUTTON_ACTION_PRESS 1U
#define EV_BUTTON_ACTION_RELEASE 2U
#define EV_BUTTON_ACTION_SHORT 3U
#define EV_BUTTON_ACTION_LONG 4U

/**
 * @brief Inline payload published when one logical panel button changes state.
 */
typedef struct {
    uint8_t button_id;
    uint8_t action;
} ev_button_event_payload_t;

EV_STATIC_ASSERT(sizeof(ev_button_event_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "Button event payload must fit into one inline event payload");

/**
 * @brief Actor-local panel state.
 */
typedef struct {
    ev_delivery_fn_t deliver;
    void *deliver_context;
    uint8_t raw_mask;
    uint8_t stable_mask;
    uint8_t long_reported_mask;
    uint8_t debounce_ticks[EV_PANEL_BUTTON_COUNT];
    uint16_t hold_ticks[EV_PANEL_BUTTON_COUNT];
    bool raw_valid;
} ev_panel_actor_ctx_t;

/**
 * @brief Initialize one logical panel actor context.
 *
 * @param ctx Context to initialize.
 * @param deliver Delivery callback used by ev_publish().
 * @param deliver_context Caller-owned context bound to @p deliver.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_panel_actor_init(ev_panel_actor_ctx_t *ctx,
                                ev_delivery_fn_t deliver,
                                void *deliver_context);

/**
 * @brief Default actor handler for one logical panel runtime instance.
 *
 * Supported events:
 * - EV_MCP23008_INPUT_CHANGED
 * - EV_TICK_100MS
 *
 * @param actor_context Pointer to ev_panel_actor_ctx_t.
 * @param msg Runtime envelope delivered to the actor.
 * @return EV_OK on success or an error code when the message contract is invalid.
 */
ev_result_t ev_panel_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_PANEL_ACTOR_H */
