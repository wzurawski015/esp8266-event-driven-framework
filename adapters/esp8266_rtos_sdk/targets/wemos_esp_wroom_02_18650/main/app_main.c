#include <stdint.h>
#include <stdio.h>

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/esp8266_boot_diag.h"
#include "ev/esp8266_runtime_app.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

EV_STATIC_ASSERT(EV_BOARD_PIN_STATUS_LED == 16U,
                 "WROOM-02 board status LED must stay fixed to GPIO16");

static const ev_demo_app_board_profile_t k_wemos_minimal_runtime_profile = {
    .capabilities_mask = EV_DEMO_APP_BOARD_CAP_DEEP_SLEEP_WAKE_GPIO16,
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
    .remote_command_token = "",
    .remote_command_capabilities = 0U,
};

void app_main(void)
{
    printf("EV_WEMOS_SMOKE_BOOT board=%s profile=minimal_runtime\n", EV_BOARD_NAME);

    static const ev_boot_diag_config_t k_boot_diag = {
        .board_tag = EV_BOARD_TAG,
        .board_name = EV_BOARD_NAME,
        .uart_port = 0U,
        .uart_baud_rate = 115200U,
        .heartbeat_period_ms = 1000U,
    };

#if EV_BOARD_RUNTIME_PROFILE_MINIMAL
    printf("EV_WEMOS_SMOKE_RUNTIME_READY\n");
    ev_esp8266_runtime_app_run(&k_boot_diag,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               NULL,
                               &k_wemos_minimal_runtime_profile);
#else
    ev_esp8266_boot_diag_run(&k_boot_diag);
#endif
}
