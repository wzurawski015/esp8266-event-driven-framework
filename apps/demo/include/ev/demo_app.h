#ifndef EV_DEMO_APP_H
#define EV_DEMO_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/lease_pool.h"
#include "ev/port_clock.h"
#include "ev/port_log.h"
#include "ev/runtime_graph.h"
#include "ev/port_net.h"
#include "ev/system_pump.h"

/* Wstrzykiwane kontrakty i Aktorzy dodani w Stage 2 */
#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/port_onewire.h"
#include "ev/ds18b20_actor.h"
#include "ev/mcp23008_actor.h"
#include "ev/network_actor.h"
#include "ev/command_actor.h"
#include "ev/oled_actor.h"
#include "ev/panel_actor.h"
#include "ev/rtc_actor.h"
#include "ev/supervisor_actor.h"
#include "ev/power_actor.h"
#include "ev/system_port.h"
#include "ev/watchdog_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_DEMO_APP_MAILBOX_CAPACITY 8U
#define EV_DEMO_APP_LEASE_SLOTS 4U
#define EV_DEMO_APP_SNAPSHOT_BYTES 16U
#define EV_DEMO_APP_LEASE_SLOT_BYTES 96U

#define EV_DEMO_APP_BOARD_CAP_I2C0 0x00000001UL
#define EV_DEMO_APP_BOARD_CAP_ONEWIRE0 0x00000002UL
#define EV_DEMO_APP_BOARD_CAP_GPIO_IRQ 0x00000004UL
#define EV_DEMO_APP_BOARD_CAP_DEEP_SLEEP_WAKE_GPIO16 0x00000008UL
#define EV_DEMO_APP_BOARD_CAP_WDT 0x00000010UL
#define EV_DEMO_APP_BOARD_CAP_NET 0x00000020UL
#define EV_DEMO_APP_BOARD_CAP_REMOTE_COMMANDS 0x00000040UL

/**
 * @brief Board-owned hardware profile consumed by the portable runtime.
 *
 * Targets build this object from board_profile.h so I2C addresses, enabled
 * hardware actors, and supervisor readiness policy stay outside app defaults.
 */
typedef struct {
    uint32_t capabilities_mask;
    uint32_t hardware_present_mask;
    uint32_t supervisor_required_mask;
    uint32_t supervisor_optional_mask;
    ev_i2c_port_num_t i2c_port_num;
    ev_irq_line_id_t rtc_sqw_line_id;
    uint8_t mcp23008_addr_7bit;
    uint8_t rtc_addr_7bit;
    uint8_t oled_addr_7bit;
    ev_oled_controller_t oled_controller;
    uint32_t watchdog_timeout_ms;
    const char *remote_command_token;
    uint32_t remote_command_capabilities;
} ev_demo_app_board_profile_t;

const ev_demo_app_board_profile_t *ev_demo_app_default_board_profile(void);

/**
 * @brief Immutable wiring required by the portable demo runtime.
 */
typedef struct {
    const char *app_tag;
    const char *board_name;
    uint32_t tick_period_ms;
    ev_clock_port_t *clock_port;
    ev_log_port_t *log_port;
    ev_irq_port_t *irq_port; /* Wstrzyknięty kontrakt wejścia IRQ dla aktorów sprzętowych. */
    ev_i2c_port_t *i2c_port; /* Wstrzyknięty kontrakt magistrali I2C dla aktorów sprzętowych. */
    ev_onewire_port_t *onewire_port; /* Wstrzyknięty kontrakt 1-Wire dla aktorów sprzętowych. */
    ev_system_port_t *system_port; /* Wstrzyknięty kontrakt globalnego stanu zasilania. */
    ev_wdt_port_t *wdt_port; /* Optional health-gated hardware watchdog mechanism. */
    ev_net_port_t *net_port; /* Optional bounded network ingress/egress mechanism. */
    const ev_demo_app_board_profile_t *board_profile; /* BSP-derived hardware graph and device policy. */
} ev_demo_app_config_t;

/**
 * @brief High-level counters emitted by the portable demo runtime.
 */
typedef struct {
    uint32_t boot_completions;
    uint32_t ticks_published;
    uint32_t diag_ticks_seen;
    uint32_t snapshots_published;
    uint32_t snapshots_received;
    uint32_t publish_errors;
    uint32_t pump_errors;
    uint32_t irq_samples_drained;
    uint32_t irq_samples_dropped_observed;
    uint32_t irq_samples_pending_high_watermark;
    uint32_t irq_ring_high_watermark_observed;
    size_t max_pending_before_poll;
    size_t max_pending_after_poll;
    size_t max_irq_samples_per_poll;
    size_t max_pump_calls_per_poll;
    size_t max_turns_per_poll;
    size_t max_messages_per_poll;
    uint32_t last_poll_elapsed_ms;
    uint32_t max_poll_elapsed_ms;
    uint32_t disabled_route_deliveries;
    uint32_t sleep_arm_attempts;
    uint32_t sleep_arm_successes;
    uint32_t sleep_arm_failures;
    uint32_t sleep_disarm_calls;
    uint32_t watchdog_disabled_route_deliveries;
    uint32_t network_disabled_route_deliveries;
    uint32_t net_ingress_drained;
    uint32_t net_events_dropped_observed;
    uint32_t net_ring_high_watermark_observed;
    uint32_t net_payload_dropped_oversize;
    uint32_t net_no_payload_slot_drops_observed;
    size_t max_net_samples_per_poll;
} ev_demo_app_stats_t;

typedef struct ev_demo_app ev_demo_app_t;

/**
 * @brief Actor-local APP state carried inside the demo runtime object.
 */
typedef struct {
    ev_demo_app_t *app;
    uint32_t last_snapshot_sequence;
    uint32_t last_diag_ticks_seen;
    ev_time_payload_t last_time;
    ev_temp_payload_t last_temp;
    bool time_valid;
    bool temp_valid;
    bool oled_frame_visible;
    bool system_ready;
    uint32_t active_hardware_mask;
    bool screensaver_paused;
    uint8_t panel_led_mask;
    uint8_t current_page_offset;
    uint8_t current_column_offset;
    uint8_t last_page_offset;
    uint8_t last_column_offset;
    int8_t direction_x;
    int8_t direction_y;
    ev_oled_scene_t oled_scene;
} ev_demo_app_actor_state_t;

/**
 * @brief Actor-local DIAG state carried inside the demo runtime object.
 */
typedef struct {
    ev_demo_app_t *app;
    uint32_t ticks_seen;
    uint32_t snapshots_sent;
    uint32_t last_tick_ms;
    uint32_t rtc_irq_samples_seen;
} ev_demo_diag_actor_state_t;

/**
 * @brief Fully static portable event-driven demo runtime.
 */
struct ev_demo_app {
    ev_clock_port_t *clock_port;
    ev_log_port_t *log_port;
    const char *app_tag;
    const char *board_name;
    uint32_t tick_period_ms;
    ev_timer_token_t tick_1s_token;
    ev_timer_token_t tick_100ms_token;
    bool standard_timers_scheduled;
    bool boot_published;
    bool sleep_arming;
    ev_irq_port_t *irq_port;
    ev_system_port_t *system_port;
    ev_wdt_port_t *wdt_port;
    ev_net_port_t *net_port;
    ev_demo_app_board_profile_t board_profile;

    ev_runtime_graph_t graph;

    ev_lease_pool_t lease_pool;
    ev_lease_slot_t lease_slots[EV_DEMO_APP_LEASE_SLOTS];
    unsigned char lease_storage[EV_DEMO_APP_LEASE_SLOTS * EV_DEMO_APP_LEASE_SLOT_BYTES];

    ev_demo_app_actor_state_t app_actor;
    ev_demo_diag_actor_state_t diag_actor;
    ev_panel_actor_ctx_t panel_ctx; /* Stan logicznego Aktora Panelu */
    ev_rtc_actor_ctx_t rtc_ctx; /* Fizyczny stan i konfiguracja Aktora RTC */
    ev_mcp23008_actor_ctx_t mcp23008_ctx; /* Fizyczny stan i konfiguracja Aktora MCP23008 */
    ev_ds18b20_actor_ctx_t ds18b20_ctx; /* Fizyczny stan i konfiguracja Aktora DS18B20 */
    ev_oled_actor_ctx_t oled_ctx; /* Fizyczny stan i bufor ekranu OLED */
    ev_supervisor_actor_ctx_t supervisor_ctx; /* Stan Supervisora platformy */
    ev_power_actor_ctx_t power_ctx; /* Stan Aktora Power */
    ev_watchdog_actor_ctx_t watchdog_ctx; /* Stan Aktora Watchdog */
    ev_network_actor_ctx_t network_ctx; /* Stan Aktora Network */
    ev_command_actor_ctx_t command_ctx; /* Authenticated remote command dispatcher state */

    ev_demo_app_stats_t stats;
};

/**
 * @brief Initialize the portable event-driven demo runtime.
 *
 * @param app Runtime object to initialize.
 * @param cfg Immutable runtime wiring.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_demo_app_init(ev_demo_app_t *app, const ev_demo_app_config_t *cfg);

/**
 * @brief Publish the startup boot events into the runtime.
 *
 * This is intentionally separated from init so tests and targets can control
 * when the first work enters the cooperative pump.
 *
 * @param app Runtime object.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_demo_app_publish_boot(ev_demo_app_t *app);

/**
 * @brief Run one bounded non-blocking poll iteration.
 *
 * The poll drains currently pending work only up to a bounded per-call budget,
 * publishes any due periodic tick events while budget remains, and then drains
 * the resulting work again within that same budget. When work remains after the
 * budget is exhausted the function returns EV_ERR_PARTIAL.
 *
 * @param app Runtime object.
 * @return EV_OK on success, EV_ERR_PARTIAL when bounded work remains, or an
 *         error code.
 */
ev_result_t ev_demo_app_poll(ev_demo_app_t *app);

/**
 * @brief Return the currently pending message count across all bound domains.
 *
 * @param app Runtime object.
 * @return Pending message count.
 */
size_t ev_demo_app_pending(const ev_demo_app_t *app);
ev_result_t ev_demo_app_next_deadline_ms(const ev_demo_app_t *app, uint32_t *out_deadline_ms);
ev_result_t ev_demo_app_post_event(ev_demo_app_t *app, ev_event_id_t event_id, ev_actor_id_t source_actor, const void *payload, size_t payload_size);

/**
 * @brief Return a stable pointer to high-level demo counters.
 *
 * @param app Runtime object.
 * @return Pointer to counters or NULL when @p app is NULL.
 */
const ev_demo_app_stats_t *ev_demo_app_stats(const ev_demo_app_t *app);

/**
 * @brief Return a stable pointer to the underlying system-pump counters.
 *
 * @param app Runtime object.
 * @return Pointer to counters or NULL when @p app is NULL.
 */
const ev_system_pump_stats_t *ev_demo_app_system_pump_stats(const ev_demo_app_t *app);
const ev_watchdog_actor_stats_t *ev_demo_app_watchdog_stats(const ev_demo_app_t *app);
const ev_network_actor_stats_t *ev_demo_app_network_stats(const ev_demo_app_t *app);
const ev_command_actor_stats_t *ev_demo_app_command_stats(const ev_demo_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_APP_H */

