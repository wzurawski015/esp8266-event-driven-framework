#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "driver/uart.h"
#include "esp_log.h"

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/demo_app.h"
#include "ev/oled_actor.h"
#include "ev/mcp23008_actor.h"
#include "ev/rtc_actor.h"
#include "ev/esp8266_boot_diag.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/esp8266_runtime_app.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

EV_STATIC_ASSERT(EV_BOARD_ONEWIRE_GPIO != EV_BOARD_IRQ_INT0_GPIO,
                 "1-Wire and RTC/INT0 ingress must stay on distinct GPIOs");
static ev_i2c_port_t s_board_i2c_port;
static ev_irq_port_t s_board_irq_port;
static ev_onewire_port_t s_board_onewire_port;
static ev_wdt_port_t s_board_wdt_port;
#if EV_BOARD_HAS_NET
static ev_net_port_t s_board_net_port;
#endif

static const ev_gpio_irq_line_config_t k_board_irq_lines[] = {
    {
        .line_id = 0U,
        .gpio_num = EV_BOARD_IRQ_INT0_GPIO,
        .trigger = EV_GPIO_IRQ_TRIGGER_FALLING,
        .pull_mode = EV_GPIO_IRQ_PULL_UP,
    },
    {
        .line_id = 1U,
        .gpio_num = EV_BOARD_IRQ_IR0_GPIO,
        .trigger = EV_GPIO_IRQ_TRIGGER_ANYEDGE,
        .pull_mode = EV_GPIO_IRQ_PULL_UP,
    },
};

#if EV_BOARD_HAS_NET
static const ev_esp8266_net_config_t k_board_net_cfg = {
    .wifi_ssid = EV_BOARD_NET_WIFI_SSID,
    .wifi_password = EV_BOARD_NET_WIFI_PASSWORD,
    .wifi_auth_mode = EV_BOARD_NET_WIFI_AUTH_MODE,
    .mqtt_broker_uri = EV_BOARD_NET_MQTT_BROKER_URI,
    .mqtt_client_id = EV_BOARD_NET_MQTT_CLIENT_ID,
};
#endif

static const ev_demo_app_board_profile_t k_board_runtime_profile = {
    .capabilities_mask = (EV_BOARD_HAS_I2C0 ? EV_DEMO_APP_BOARD_CAP_I2C0 : 0U) |
                         (EV_BOARD_HAS_ONEWIRE0 ? EV_DEMO_APP_BOARD_CAP_ONEWIRE0 : 0U) |
                         (EV_BOARD_HAS_GPIO_IRQ ? EV_DEMO_APP_BOARD_CAP_GPIO_IRQ : 0U) |
                         (EV_BOARD_HAS_DEEP_SLEEP_WAKE_GPIO16 ? EV_DEMO_APP_BOARD_CAP_DEEP_SLEEP_WAKE_GPIO16 : 0U) |
                         (EV_BOARD_HAS_WDT ? EV_DEMO_APP_BOARD_CAP_WDT : 0U) |
                         (EV_BOARD_HAS_NET ? EV_DEMO_APP_BOARD_CAP_NET : 0U),
    .hardware_present_mask = EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK,
    .supervisor_required_mask = EV_BOARD_SUPERVISOR_REQUIRED_MASK,
    .supervisor_optional_mask = EV_BOARD_SUPERVISOR_OPTIONAL_MASK,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = EV_BOARD_RTC_SQW_LINE_ID,
    .mcp23008_addr_7bit = EV_BOARD_MCP23008_ADDR_7BIT,
    .rtc_addr_7bit = EV_BOARD_RTC_ADDR_7BIT,
    .oled_addr_7bit = EV_BOARD_OLED_ADDR_7BIT,
    .oled_controller = EV_BOARD_OLED_CONTROLLER,
    .watchdog_timeout_ms = EV_BOARD_WDT_TIMEOUT_MS,
    .remote_command_token = EV_BOARD_NET_COMMAND_TOKEN,
    .remote_command_capabilities = EV_BOARD_REMOTE_COMMAND_CAPABILITIES,
};

void app_main(void)
{
    static const ev_boot_diag_config_t k_boot_diag = {
        .board_tag = EV_BOARD_TAG,
        .board_name = EV_BOARD_NAME,
        .uart_port = 0U,
        .uart_baud_rate = 115200U,
        .heartbeat_period_ms = 1000U,
    };
    ev_i2c_port_t *runtime_i2c_port = NULL;
    ev_irq_port_t *runtime_irq_port = NULL;
    ev_onewire_port_t *runtime_onewire_port = NULL;
    ev_wdt_port_t *runtime_wdt_port = NULL;
    ev_net_port_t *runtime_net_port = NULL;
    ev_result_t rc;

    (void)uart_set_baudrate(UART_NUM_0, 115200U);

    rc = ev_esp8266_i2c_port_init(&s_board_i2c_port, EV_BOARD_I2C_SDA_GPIO, EV_BOARD_I2C_SCL_GPIO);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "i2c adapter init failed rc=%d", (int)rc);
    } else {
        runtime_i2c_port = &s_board_i2c_port;
    }

    rc = ev_esp8266_onewire_port_init(&s_board_onewire_port, EV_BOARD_ONEWIRE_GPIO);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "onewire adapter init failed rc=%d", (int)rc);
    } else {
        runtime_onewire_port = &s_board_onewire_port;
    }

    rc = ev_esp8266_irq_port_init(&s_board_irq_port,
                                  k_board_irq_lines,
                                  (sizeof(k_board_irq_lines) / sizeof(k_board_irq_lines[0])));
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "irq adapter init failed rc=%d", (int)rc);
    } else {
        runtime_irq_port = &s_board_irq_port;
    }

    rc = ev_esp8266_wdt_port_init(&s_board_wdt_port);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "wdt adapter init failed rc=%d", (int)rc);
    } else {
        runtime_wdt_port = &s_board_wdt_port;
    }

#if EV_BOARD_HAS_NET
    rc = ev_esp8266_net_port_init(&s_board_net_port, &k_board_net_cfg);
    if (rc != EV_OK) {
        ESP_LOGE(EV_BOARD_TAG, "net adapter init failed rc=%d", (int)rc);
    } else {
        runtime_net_port = &s_board_net_port;
    }
#endif

    ev_esp8266_runtime_app_run(&k_boot_diag,
                               runtime_i2c_port,
                               runtime_irq_port,
                               runtime_onewire_port,
                               runtime_wdt_port,
                               runtime_net_port,
                               &k_board_runtime_profile);
}
