#include <assert.h>

#include "route_test_utils.h"

int main(void)
{
    static const ev_actor_id_t boot_bound_actors[] = {
        ACT_DIAG,
        ACT_APP,
        ACT_MCP23008,
        ACT_RTC,
        ACT_DS18B20,
        ACT_BH1750,
        ACT_OLED,
        ACT_SUPERVISOR,
    };

    assert(ev_test_route_count_for_event(EV_BOOT_COMPLETED) == sizeof(boot_bound_actors) / sizeof(boot_bound_actors[0]));
    assert(ev_test_actor_is_in_set(ACT_BH1750, boot_bound_actors, sizeof(boot_bound_actors) / sizeof(boot_bound_actors[0])));
    ev_test_assert_event_targets_are_bound(
        EV_BOOT_COMPLETED,
        boot_bound_actors,
        sizeof(boot_bound_actors) / sizeof(boot_bound_actors[0]));
    return 0;
}
