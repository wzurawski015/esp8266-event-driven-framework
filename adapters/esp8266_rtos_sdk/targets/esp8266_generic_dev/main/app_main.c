#include <stdint.h>

#include "board_profile.h"

#include "ev/compiler.h"
#include "ev/esp8266_boot_diag.h"

#define EV_BOARD_TAG EV_BOARD_PROFILE_TAG
#define EV_BOARD_NAME EV_BOARD_PROFILE_NAME

EV_STATIC_ASSERT(EV_BOARD_PIN_STATUS_LED == 2U,
                 "Generic development board status LED mapping must stay fixed to GPIO2");

void app_main(void)
{
    static const ev_boot_diag_config_t k_boot_diag = {
        .board_tag = EV_BOARD_TAG,
        .board_name = EV_BOARD_NAME,
        .uart_port = 0U,
        .uart_baud_rate = 115200U,
        .heartbeat_period_ms = 1000U,
    };

    ev_esp8266_boot_diag_run(&k_boot_diag);
}
