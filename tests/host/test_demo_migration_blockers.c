#include <assert.h>
#include <stdio.h>
#include <string.h>

static int file_contains(const char *path, const char *needle)
{
    char buf[512];
    FILE *f = fopen(path, "r");
    if (f == 0) {
        return 0;
    }
    while (fgets(buf, sizeof(buf), f) != 0) {
        if (strstr(buf, needle) != 0) {
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

int main(void)
{
    assert(file_contains("apps/demo/include/ev/demo_app.h", "ev_mailbox_t") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "ev_actor_runtime_t") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "ev_actor_registry_t") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "ev_domain_pump_t") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "ev_system_pump_t") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "next_tick_ms") == 0);
    assert(file_contains("apps/demo/include/ev/demo_app.h", "next_tick_100ms_ms") == 0);
    assert(file_contains("apps/demo/ev_demo_app.c", "ev_actor_registry_bind") == 0);
    assert(file_contains("apps/demo/ev_demo_app.c", "ev_domain_pump_init") == 0);
    assert(file_contains("apps/demo/ev_demo_app.c", "ev_system_pump_init") == 0);
    assert(file_contains("apps/demo/ev_demo_app.c", "ev_system_pump_run") == 0);
    assert(file_contains("adapters/esp8266_rtos_sdk/components/ev_platform/ev_runtime_app.c", "next_tick_ms") == 0);
    assert(file_contains("adapters/esp8266_rtos_sdk/components/ev_platform/ev_runtime_app.c", "next_tick_100ms_ms") == 0);
    return 0;
}
