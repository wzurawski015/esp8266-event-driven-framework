#include <stdint.h>
#include <stdio.h>

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/esp8266_boot_diag.h"
#include "ev/esp8266_port_adapters.h"
#include "ev/esp8266_runtime_app.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

EV_STATIC_ASSERT(EV_BOARD_PIN_STATUS_LED == 16U,
                 "WROOM-02 board status LED must stay fixed to GPIO16");

#if EV_BOARD_HAS_NET
static ev_net_port_t s_board_net_port;

static const ev_esp8266_net_config_t k_board_net_cfg = {
    .wifi_ssid = EV_BOARD_NET_WIFI_SSID,
    .wifi_password = EV_BOARD_NET_WIFI_PASSWORD,
    .wifi_auth_mode = EV_BOARD_NET_WIFI_AUTH_MODE,
    .mqtt_broker_uri = EV_BOARD_NET_MQTT_BROKER_URI,
    .mqtt_client_id = EV_BOARD_NET_MQTT_CLIENT_ID,
};
#endif

static const ev_demo_app_board_profile_t k_wemos_minimal_runtime_profile = {
    .capabilities_mask = EV_DEMO_APP_BOARD_CAP_DEEP_SLEEP_WAKE_GPIO16 |
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
    .watchdog_timeout_ms = 0U,
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
    ev_net_port_t *runtime_net_port = NULL;

    printf("EV_WEMOS_SMOKE_BOOT board=%s profile=minimal_runtime\n", EV_BOARD_NAME);

#if EV_BOARD_HAS_NET
    {
        const ev_result_t rc = ev_esp8266_net_port_init(&s_board_net_port, &k_board_net_cfg);
        if (rc == EV_OK) {
            runtime_net_port = &s_board_net_port;
        } else {
            printf("EV_WEMOS_NET_PORT_INIT_FAILED rc=%d\n", (int)rc);
        }
    }
#endif

    printf("EV_WEMOS_SMOKE_RUNTIME_READY\n");
    ev_esp8266_runtime_app_run(&k_boot_diag,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               runtime_net_port,
                               &k_wemos_minimal_runtime_profile);
}
