#ifndef EV_COMMAND_ACTOR_H
#define EV_COMMAND_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_COMMAND_CAP_LED 0x00000001UL
#define EV_COMMAND_CAP_DISPLAY 0x00000002UL
#define EV_COMMAND_CAP_SLEEP 0x00000004UL

#ifndef EV_COMMAND_TOKEN_MAX_BYTES
#define EV_COMMAND_TOKEN_MAX_BYTES 32U
#endif
#ifndef EV_COMMAND_TOPIC_MAX_BYTES
#define EV_COMMAND_TOPIC_MAX_BYTES 16U
#endif
#ifndef EV_COMMAND_PAYLOAD_MAX_BYTES
#define EV_COMMAND_PAYLOAD_MAX_BYTES 128U
#endif
#ifndef EV_COMMAND_SLEEP_NONCE_MAX_BYTES
#define EV_COMMAND_SLEEP_NONCE_MAX_BYTES 16U
#endif
#ifndef EV_COMMAND_MIN_INTERVAL_TICKS
#define EV_COMMAND_MIN_INTERVAL_TICKS 1U
#endif
#ifndef EV_COMMAND_SLEEP_CONFIRM_WINDOW_TICKS
#define EV_COMMAND_SLEEP_CONFIRM_WINDOW_TICKS 30U
#endif
#ifndef EV_COMMAND_DISPLAY_PAGE
#define EV_COMMAND_DISPLAY_PAGE 0U
#endif
#ifndef EV_COMMAND_DISPLAY_COLUMN
#define EV_COMMAND_DISPLAY_COLUMN 0U
#endif

typedef struct ev_command_actor_stats {
    uint32_t ticks_seen;
    uint32_t rx_seen;
    uint32_t rx_inline_seen;
    uint32_t rx_lease_seen;
    uint32_t unknown_topic;
    uint32_t auth_rejects;
    uint32_t disabled_rejects;
    uint32_t capability_rejects;
    uint32_t parse_errors;
    uint32_t rate_limited;
    uint32_t commands_executed;
    uint32_t led_commands;
    uint32_t display_commands;
    uint32_t sleep_arm_requests;
    uint32_t sleep_confirm_requests;
    uint32_t sleep_commands;
    uint32_t sleep_expired;
    uint32_t sleep_confirm_mismatch;
    uint32_t publish_failures;
} ev_command_actor_stats_t;

typedef struct ev_command_actor_ctx {
    ev_delivery_fn_t deliver;
    void *deliver_context;
    const char *token;
    size_t token_len;
    uint32_t capabilities;
    uint32_t tick_counter;
    uint32_t last_led_tick;
    uint32_t last_display_tick;
    uint32_t last_sleep_tick;
    bool sleep_armed;
    uint32_t sleep_duration_ms;
    uint32_t sleep_expiry_tick;
    uint8_t sleep_nonce_len;
    char sleep_nonce[EV_COMMAND_SLEEP_NONCE_MAX_BYTES];
    ev_command_actor_stats_t stats;
} ev_command_actor_ctx_t;

ev_result_t ev_command_actor_init(ev_command_actor_ctx_t *ctx,
                                  ev_delivery_fn_t deliver,
                                  void *deliver_context,
                                  const char *token,
                                  uint32_t capabilities);

ev_result_t ev_command_actor_handle(void *actor_context, const ev_msg_t *msg);

const ev_command_actor_stats_t *ev_command_actor_stats(const ev_command_actor_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EV_COMMAND_ACTOR_H */
