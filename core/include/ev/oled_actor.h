#ifndef EV_OLED_ACTOR_H
#define EV_OLED_ACTOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/port_i2c.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_OLED_WIDTH 72U
#define EV_OLED_HEIGHT 40U
#define EV_OLED_PAGE_COUNT (EV_OLED_HEIGHT / 8U)
#define EV_OLED_DEFAULT_ADDR_7BIT 0x3CU
#define EV_OLED_TEXT_MAX_CHARS 22U
#define EV_OLED_RETRY_DELAY_TICKS 5U
#define EV_OLED_SCENE_LINE_COUNT 3U
#define EV_OLED_SCENE_FLAG_VISIBLE 0x01U

/**
 * @brief Supported controller families for the first OLED actor stage.
 */
typedef enum ev_oled_controller {
    EV_OLED_CONTROLLER_SSD1306 = 0,
    EV_OLED_CONTROLLER_SH1106 = 1
} ev_oled_controller_t;

/**
 * @brief High-level lifecycle state of the OLED actor.
 */
typedef enum ev_oled_actor_state {
    EV_OLED_STATE_UNINITIALIZED = 0,
    EV_OLED_STATE_WAIT_BOOT = 1,
    EV_OLED_STATE_READY = 2,
    EV_OLED_STATE_ERROR = 3
} ev_oled_actor_state_t;

/**
 * @brief Inline payload used by EV_OLED_DISPLAY_TEXT_CMD.
 *
 * The command replaces the logical line fragment starting at @p column on one
 * display page. The actor clears all pixels from @p column to the end of the
 * page before rendering @p text.
 */
typedef struct {
    uint8_t page;
    uint8_t column;
    char text[EV_OLED_TEXT_MAX_CHARS];
} ev_oled_display_text_cmd_t;

EV_STATIC_ASSERT(sizeof(ev_oled_display_text_cmd_t) <= EV_MSG_INLINE_CAPACITY,
                 "OLED text command must fit into one inline event payload");


/**
 * @brief Lease-backed scene payload committed to the OLED actor.
 *
 * The application composes one complete logical frame in this structure and
 * publishes a single EV_OLED_COMMIT_FRAME message. The OLED actor then diffs
 * the committed scene against its currently displayed framebuffer and flushes
 * only the changed page ranges.
 */
typedef struct {
    uint8_t page_offset;
    uint8_t column_offset;
    uint8_t flags;
    uint8_t reserved;
    char lines[EV_OLED_SCENE_LINE_COUNT][EV_OLED_TEXT_MAX_CHARS];
} ev_oled_scene_t;


/**
 * @brief Runtime counters owned by one OLED actor instance.
 */
typedef struct {
    uint32_t boot_events_seen;
    uint32_t tick_events_seen;
    uint32_t display_commands_seen;
    uint32_t init_attempts;
    uint32_t init_successes;
    uint32_t init_failures;
    uint32_t flush_attempts;
    uint32_t flush_successes;
    uint32_t flush_failures;
    uint32_t retries_scheduled;
    ev_i2c_status_t last_i2c_status;
} ev_oled_actor_stats_t;

/**
 * @brief Actor-local OLED state.
 *
 * The context is fully portable. It stores only the injected I2C contract,
 * current state-machine progress, and the local framebuffer cache required to
 * redraw the active OLED panel geometry.
 */
typedef struct {
    ev_i2c_port_t *i2c_port;
    ev_delivery_fn_t deliver;
    void *deliver_context;
    ev_i2c_port_num_t port_num;
    uint8_t device_address_7bit;
    ev_oled_controller_t controller;
    ev_oled_actor_state_t state;
    ev_i2c_status_t last_i2c_status;
    uint32_t tick_counter;
    uint32_t retry_due_tick;
    uint32_t retry_delay_ticks;
    bool boot_observed;
    bool pending_flush;
    uint8_t dirty_page_mask;
    uint8_t dirty_column_start[EV_OLED_PAGE_COUNT];
    uint8_t dirty_column_end[EV_OLED_PAGE_COUNT];
    uint8_t framebuffer[EV_OLED_PAGE_COUNT][EV_OLED_WIDTH];
    uint8_t staging_framebuffer[EV_OLED_PAGE_COUNT][EV_OLED_WIDTH];
    ev_oled_actor_stats_t stats;
} ev_oled_actor_ctx_t;

/**
 * @brief Initialize one OLED actor context.
 *
 * The function performs no hardware I/O. The actual panel initialization is
 * deferred until the actor observes EV_BOOT_COMPLETED and then retried from
 * EV_TICK_1S on failures.
 *
 * @param ctx Context to initialize.
 * @param i2c_port Injected platform I2C contract.
 * @param port_num Logical I2C controller number.
 * @param device_address_7bit Target 7-bit OLED I2C address.
 * @param controller Selected controller family.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_oled_actor_init(ev_oled_actor_ctx_t *ctx,
                               ev_i2c_port_t *i2c_port,
                               ev_i2c_port_num_t port_num,
                               uint8_t device_address_7bit,
                               ev_oled_controller_t controller,
                               ev_delivery_fn_t deliver,
                               void *deliver_context);

/**
 * @brief Default actor handler for one OLED runtime instance.
 *
 * Supported events:
 * - EV_BOOT_COMPLETED
 * - EV_TICK_1S
 * - EV_OLED_DISPLAY_TEXT_CMD
 * - EV_OLED_COMMIT_FRAME
 *
 * The actor publishes EV_OLED_READY after the first successful initialization.
 *
 * EV_OLED_COMMIT_FRAME may carry one lease-backed ::ev_oled_scene_t payload.
 * The actor diffs the committed scene against the currently displayed
 * framebuffer and flushes only the dirty page regions.
 *
 * Transport failures never block the runtime. The actor moves into
 * EV_OLED_STATE_ERROR, latches the affected dirty range, and retries the panel
 * initialization after a bounded number of system ticks.
 *
 * @param actor_context Pointer to ev_oled_actor_ctx_t.
 * @param msg Runtime envelope delivered to the actor.
 * @return EV_OK on success or an error code when the message contract is invalid.
 */
ev_result_t ev_oled_actor_handle(void *actor_context, const ev_msg_t *msg);

/**
 * @brief Return a stable pointer to OLED actor counters.
 *
 * @param ctx Actor context.
 * @return Pointer to counters or NULL when @p ctx is NULL.
 */
const ev_oled_actor_stats_t *ev_oled_actor_stats(const ev_oled_actor_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* EV_OLED_ACTOR_H */
