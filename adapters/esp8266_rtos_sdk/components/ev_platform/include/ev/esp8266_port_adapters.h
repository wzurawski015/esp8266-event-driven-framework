#ifndef EV_ESP8266_PORT_ADAPTERS_H
#define EV_ESP8266_PORT_ADAPTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/port_clock.h"
#include "ev/port_i2c.h"
#include "ev/port_irq.h"
#include "ev/port_gpio_irq.h"
#include "ev/port_onewire.h"
#include "ev/port_log.h"
#include "ev/port_net.h"
#include "ev/port_reset.h"
#include "ev/port_uart.h"
#include "ev/port_wdt.h"
#include "ev/system_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Snapshot of private ESP8266 zero-heap I2C adapter counters.
 */
typedef struct ev_esp8266_i2c_diag_snapshot {
    uint32_t transactions_started; /**< Number of runtime I2C transactions that acquired the bus. */
    uint32_t transactions_failed; /**< Number of runtime I2C transactions that ended with non-OK status. */
    uint32_t nacks; /**< Number of normalized NACK outcomes. */
    uint32_t timeouts; /**< Number of bounded timeout outcomes. */
    uint32_t bus_locked; /**< Number of bus-locked or unsafe-bus outcomes. */
    uint32_t bus_recoveries; /**< Number of attempted bounded bus-recovery sequences. */
    uint32_t bus_recovery_failures; /**< Number of bus-recovery attempts that did not restore idle bus state. */
    uint32_t sleep_prepare_attempts; /**< Number of bounded sleep-prepare checks touching the I2C bus. */
    uint32_t sleep_prepare_failures; /**< Number of I2C sleep-prepare rejections. */
    bool transaction_active; /**< True while a runtime transaction owns the software I2C master. */
    bool sda_high; /**< Last sampled SDA idle level. */
    bool scl_high; /**< Last sampled SCL idle level. */
} ev_esp8266_i2c_diag_snapshot_t;

/**
 * @brief Snapshot of private ESP8266 IRQ ingress ring counters.
 */
typedef struct ev_esp8266_irq_diag_snapshot {
    uint32_t write_seq; /**< Monotonic IRQ ring write sequence. */
    uint32_t read_seq; /**< Monotonic IRQ ring read sequence. */
    uint32_t pending_samples; /**< Number of currently pending IRQ samples. */
    uint32_t dropped_samples; /**< Number of IRQ samples dropped because the ring was full. */
    uint32_t high_watermark; /**< Maximum observed pending IRQ ring depth. */
    uint32_t active_gpio_mask; /**< GPIO bit mask currently accepted by the ISR. */
    uint32_t enabled_gpio_mask; /**< GPIO bit mask currently armed for interrupts. */
    uint32_t sleep_prepare_attempts; /**< Number of IRQ sleep-prepare attempts. */
    uint32_t sleep_prepare_failures; /**< Number of IRQ sleep-prepare rejections. */
    uint32_t sleep_prepared_gpio_mask; /**< GPIO mask disabled during a prepared sleep transition. */
    bool sleep_prepared; /**< True after IRQ ingress has been frozen for sleep. */
} ev_esp8266_irq_diag_snapshot_t;

/**
 * @brief Snapshot of private ESP8266 1-Wire adapter state.
 */
typedef struct ev_esp8266_onewire_diag_snapshot {
    uint32_t operations_started; /**< Number of 1-Wire reset/read/write operations entered. */
    uint32_t sleep_prepare_attempts; /**< Number of 1-Wire sleep-prepare checks. */
    uint32_t sleep_prepare_failures; /**< Number of 1-Wire sleep-prepare rejections. */
    uint32_t bus_errors; /**< Number of observed 1-Wire bus errors or unsafe bus states. */
    uint32_t critical_sections; /**< Number of measured scheduler-protected timing-critical 1-Wire sections. */
    uint32_t reset_critical_sections; /**< Number of measured reset-pulse scheduler-protected timing sections. */
    uint32_t bit_critical_sections; /**< Number of measured bit-slot scheduler-protected timing sections. */
    uint32_t max_critical_section_us; /**< Maximum measured scheduler-protected timing section duration. */
    uint32_t max_reset_critical_section_us; /**< Maximum measured reset-pulse scheduler-protected timing section duration. */
    uint32_t max_bit_critical_section_us; /**< Maximum measured bit-slot scheduler-protected timing section duration. */
    uint32_t critical_section_budget_violations; /**< Scheduler-protected timing sections exceeding the configured 1-Wire timing budget. */
    uint32_t max_reset_low_hold_us; /**< Maximum reset low pulse duration measured during the protected reset waveform. */
    bool configured; /**< True after the adapter was initialized. */
    bool busy; /**< True while a bit-banged 1-Wire operation is active. */
    bool dq_high; /**< Last sampled released DQ line level. */
} ev_esp8266_onewire_diag_snapshot_t;

/**
 * @brief Initialize the ESP8266-backed clock adapter.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_clock_port_init(ev_clock_port_t *out_port);

/**
 * @brief Initialize the ESP8266-backed I2C adapter.
 *
 * The adapter owns a zero-heap GPIO open-drain software I2C master and creates
 * one bootstrap-time global bus mutex used to serialize all bounded
 * transactions.  Runtime calls do not allocate SDK command links.
 *
 * @param out_port Destination public contract populated on success.
 * @param sda_pin GPIO number used for SDA.
 * @param scl_pin GPIO number used for SCL.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_i2c_port_init(ev_i2c_port_t *out_port, int sda_pin, int scl_pin);

/**
 * @brief Copy private ESP8266 I2C adapter counters for diagnostics and HIL gates.
 *
 * @param port_num Logical I2C controller identifier.
 * @param out_snapshot Destination snapshot populated on success.
 * @return EV_OK on success or EV_ERR_INVALID_ARG/EV_ERR_STATE.
 */
ev_result_t ev_esp8266_i2c_get_diag(ev_i2c_port_num_t port_num, ev_esp8266_i2c_diag_snapshot_t *out_snapshot);

/**
 * @brief Verify and park the ESP8266 zero-heap I2C bus before Deep Sleep.
 *
 * @param port_num Logical I2C controller identifier.
 * @return EV_OK when SDA/SCL are released and idle, otherwise a bounded rejection.
 */
ev_result_t ev_esp8266_i2c_prepare_for_sleep(ev_i2c_port_num_t port_num);

/**
 * @brief Scan one initialized I2C controller and log detected slaves.
 *
 * The scan performs bounded address-only probes across the public 7-bit I2C
 * address range and annotates the known Stage 1 motherboard devices in the
 * diagnostic log stream.
 *
 * @param port_num Logical I2C controller identifier.
 * @return EV_OK when the scan completed, even if no device acknowledged.
 */
ev_result_t ev_i2c_scan(ev_i2c_port_num_t port_num);

/**
 * @brief Initialize the ESP8266-backed GPIO interrupt ingress adapter.
 *
 * The adapter owns one static ring buffer populated from a very small ISR. The
 * public Core-facing contract exposes normalized interrupt samples through
 * ev_irq_port_t::pop and explicit per-line arming through ev_irq_port_t::enable.
 *
 * @param out_port Destination public contract populated on success.
 * @param line_cfgs Static line-to-GPIO mappings owned by the caller.
 * @param line_count Number of entries in @p line_cfgs.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_irq_port_init(ev_irq_port_t *out_port,
                                     const ev_gpio_irq_line_config_t *line_cfgs,
                                     size_t line_count);

/**
 * @brief Copy private ESP8266 IRQ ring counters for diagnostics and HIL gates.
 *
 * @param out_snapshot Destination snapshot populated on success.
 * @return EV_OK on success or EV_ERR_INVALID_ARG/EV_ERR_STATE.
 */
ev_result_t ev_esp8266_irq_get_diag(ev_esp8266_irq_diag_snapshot_t *out_snapshot);

/**
 * @brief Disable configured GPIO IRQ sources after proving the IRQ ring is empty.
 *
 * @return EV_OK when IRQ ingress is parked for Deep Sleep.
 */
ev_result_t ev_esp8266_irq_prepare_for_sleep(void);
ev_result_t ev_esp8266_irq_confirm_sleep_ready(void);
ev_result_t ev_esp8266_irq_abort_sleep_prepare(void);
ev_result_t ev_esp8266_irq_commit_sleep_prepare(void);

/**
 * @brief Initialize the ESP8266-backed 1-Wire adapter.
 *
 * The adapter configures one open-drain GPIO line used later by the DS18B20
 * actor through the portable 1-Wire contract.
 *
 * @param out_port Destination public contract populated on success.
 * @param data_pin GPIO number used for the shared 1-Wire data line.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_onewire_port_init(ev_onewire_port_t *out_port, int data_pin);

/**
 * @brief Copy private ESP8266 1-Wire adapter state for diagnostics.
 */
ev_result_t ev_esp8266_onewire_get_diag(ev_esp8266_onewire_diag_snapshot_t *out_snapshot);

/**
 * @brief Release and validate the 1-Wire DQ line before Deep Sleep.
 */
ev_result_t ev_esp8266_onewire_prepare_for_sleep(void);

/**
 * @brief Initialize the ESP8266-backed log adapter.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_log_port_init(ev_log_port_t *out_port);

/**
 * @brief Initialize the ESP8266-backed reset adapter.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_reset_port_init(ev_reset_port_t *out_port);


/**
 * @brief Immutable ESP8266 network adapter configuration built by the target BSP.
 *
 * The adapter consumes board-owned strings from board_profile.h through target
 * composition code.  This keeps WiFi/MQTT credentials out of the portable core
 * and avoids making ev_platform depend directly on any single BSP directory.
 */
typedef struct ev_esp8266_net_config {
    const char *wifi_ssid;
    const char *wifi_password;
    uint32_t wifi_auth_mode;
    const char *mqtt_broker_uri;
    const char *mqtt_client_id;
} ev_esp8266_net_config_t;

/**
 * @brief Initialize the ESP8266 WiFi/MQTT adapter behind the HSHA network airlock.
 *
 * SDK callbacks never call core, actors, mailboxes, or ev_publish().  They only
 * push bounded network ingress samples into an internal static ring.  The app
 * poll loop drains the ring through ev_net_port_t::poll_ingress.
 *
 * @param out_port Destination contract populated on success.
 * @param cfg BSP-derived WiFi/MQTT configuration.
 * @return EV_OK on successful binding or an error code.
 */
ev_result_t ev_esp8266_net_port_init(ev_net_port_t *out_port, const ev_esp8266_net_config_t *cfg);

/**
 * @brief Initialize the ESP8266-backed watchdog mechanism adapter.
 *
 * The current ESP8266 RTOS SDK headers bundled in this repository do not expose
 * a verified hardware-watchdog feed API. The adapter therefore reports
 * unsupported unless a future target enables a verified vendor implementation.
 * The actor policy is still fully testable through the portable WDT contract.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_wdt_port_init(ev_wdt_port_t *out_port);

/**
 * @brief Initialize the ESP8266-backed UART adapter.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_uart_port_init(ev_uart_port_t *out_port);

#define EV_ESP8266_SLEEP_RESOURCE_I2C0 0x00000001UL
#define EV_ESP8266_SLEEP_RESOURCE_ONEWIRE0 0x00000002UL
#define EV_ESP8266_SLEEP_RESOURCE_GPIO_IRQ 0x00000004UL
#define EV_ESP8266_SLEEP_RESOURCE_WAKE_GPIO16 0x00000008UL

/**
 * @brief Board-derived resources that the ESP8266 system adapter must park before sleep.
 */
typedef struct ev_esp8266_system_sleep_profile {
    uint32_t resource_mask;
    ev_i2c_port_num_t i2c_port_num;
} ev_esp8266_system_sleep_profile_t;

/**
 * @brief Initialize the ESP8266-backed system-control adapter.
 *
 * @param out_port Destination contract populated on success.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_system_port_init(ev_system_port_t *out_port);

/**
 * @brief Initialize the ESP8266 system-control adapter with a board-aware sleep profile.
 *
 * @param out_port Destination contract populated on success.
 * @param sleep_profile Board-derived resources to check and park before deep sleep.
 * @return EV_OK on success or an error code.
 */
ev_result_t ev_esp8266_system_port_init_with_sleep_profile(
    ev_system_port_t *out_port,
    const ev_esp8266_system_sleep_profile_t *sleep_profile);

/**
 * @brief Convert one normalized reset reason into a stable diagnostic string.
 *
 * @param reason Reset reason to render.
 * @return Stable string literal.
 */
const char *ev_reset_reason_to_cstr(ev_reset_reason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* EV_ESP8266_PORT_ADAPTERS_H */
