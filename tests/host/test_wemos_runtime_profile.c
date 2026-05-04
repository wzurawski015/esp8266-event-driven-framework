#include <assert.h>

#include "ev/demo_app.h"
#include "../../bsp/wemos_esp_wroom_02_18650/board_profile.h"

int main(void)
{
    assert(EV_BOARD_RUNTIME_PROFILE_BOOT_DIAG == 1U);
    assert(EV_BOARD_RUNTIME_PROFILE_MINIMAL == 1U);
    assert(EV_BOARD_RUNTIME_PROFILE_FULL == 0U);
    assert(EV_BOARD_RUNTIME_HARDWARE_PRESENT_MASK == 0U);
    assert(EV_BOARD_SUPERVISOR_REQUIRED_MASK == 0U);
    assert(EV_BOARD_SUPERVISOR_OPTIONAL_MASK == 0U);
    assert(EV_BOARD_HAS_ONEWIRE0 == 0U);
    assert(EV_BOARD_HAS_WDT == 0U);
    assert(EV_BOARD_HAS_NET == 0U);
    return 0;
}
