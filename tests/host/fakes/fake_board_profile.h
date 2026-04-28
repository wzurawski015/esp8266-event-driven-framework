#ifndef TESTS_HOST_FAKES_FAKE_BOARD_PROFILE_H
#define TESTS_HOST_FAKES_FAKE_BOARD_PROFILE_H

#include "ev/demo_app.h"

#ifdef __cplusplus
extern "C" {
#endif

static const ev_demo_app_board_profile_t k_fake_full_board_profile = {
    .capabilities_mask = EV_DEMO_APP_BOARD_CAP_I2C0 |
                         EV_DEMO_APP_BOARD_CAP_ONEWIRE0 |
                         EV_DEMO_APP_BOARD_CAP_GPIO_IRQ,
    .hardware_present_mask = EV_SUPERVISOR_HW_MCP23008 |
                             EV_SUPERVISOR_HW_RTC |
                             EV_SUPERVISOR_HW_OLED |
                             EV_SUPERVISOR_HW_DS18B20,
    .supervisor_required_mask = EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC,
    .supervisor_optional_mask = EV_SUPERVISOR_HW_OLED | EV_SUPERVISOR_HW_DS18B20,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = 0U,
    .mcp23008_addr_7bit = EV_MCP23008_DEFAULT_ADDR_7BIT,
    .rtc_addr_7bit = EV_RTC_DEFAULT_ADDR_7BIT,
    .oled_addr_7bit = EV_OLED_DEFAULT_ADDR_7BIT,
    .oled_controller = EV_OLED_CONTROLLER_SSD1306,
};

#ifdef __cplusplus
}
#endif

#endif /* TESTS_HOST_FAKES_FAKE_BOARD_PROFILE_H */
