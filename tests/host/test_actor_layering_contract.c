#include <assert.h>
#include <stdio.h>

#include "ev/actor_module.h"
#include "ev/command_actor.h"
#include "ev/ds18b20_actor.h"
#include "ev/mcp23008_actor.h"
#include "ev/network_actor.h"
#include "ev/oled_actor.h"
#include "ev/panel_actor.h"
#include "ev/power_actor.h"
#include "ev/rtc_actor.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_inspection.h"
#include "ev/supervisor_actor.h"
#include "ev/watchdog_actor.h"

static int file_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

static void assert_actor_file_layout(void)
{
    static const char *const required_paths[] = {
        "actors/device/ev_rtc_actor.c",
        "actors/device/ev_ds18b20_actor.c",
        "actors/device/ev_mcp23008_actor.c",
        "actors/device/ev_oled_actor.c",
        "actors/device/ev_panel_actor.c",
        "actors/device/include/ev/rtc_actor.h",
        "actors/device/include/ev/ds18b20_actor.h",
        "actors/device/include/ev/mcp23008_actor.h",
        "actors/device/include/ev/oled_actor.h",
        "actors/device/include/ev/panel_actor.h",
        "actors/framework/ev_network_actor.c",
        "actors/framework/ev_command_actor.c",
        "actors/framework/ev_power_actor.c",
        "actors/framework/ev_watchdog_actor.c",
        "actors/framework/ev_supervisor_actor.c",
        "actors/framework/include/ev/network_actor.h",
        "actors/framework/include/ev/command_actor.h",
        "actors/framework/include/ev/power_actor.h",
        "actors/framework/include/ev/watchdog_actor.h",
        "actors/framework/include/ev/supervisor_actor.h",
    };
    static const char *const forbidden_core_paths[] = {
        "core/src/ev_rtc_actor.c",
        "core/src/ev_ds18b20_actor.c",
        "core/src/ev_mcp23008_actor.c",
        "core/src/ev_oled_actor.c",
        "core/src/ev_panel_actor.c",
        "core/src/ev_network_actor.c",
        "core/src/ev_command_actor.c",
        "core/src/ev_power_actor.c",
        "core/src/ev_watchdog_actor.c",
        "core/src/ev_supervisor_actor.c",
        "core/include/ev/rtc_actor.h",
        "core/include/ev/ds18b20_actor.h",
        "core/include/ev/mcp23008_actor.h",
        "core/include/ev/oled_actor.h",
        "core/include/ev/panel_actor.h",
        "core/include/ev/network_actor.h",
        "core/include/ev/command_actor.h",
        "core/include/ev/power_actor.h",
        "core/include/ev/watchdog_actor.h",
        "core/include/ev/supervisor_actor.h",
    };
    size_t i;

    for (i = 0U; i < sizeof required_paths / sizeof required_paths[0]; ++i) {
        assert(file_exists(required_paths[i]));
    }
    for (i = 0U; i < sizeof forbidden_core_paths / sizeof forbidden_core_paths[0]; ++i) {
        assert(!file_exists(forbidden_core_paths[i]));
    }
}

static void assert_module_handlers_survived_relocation(void)
{
    static const ev_actor_id_t moved_actors[] = {
        ACT_RTC,
        ACT_DS18B20,
        ACT_MCP23008,
        ACT_OLED,
        ACT_PANEL,
        ACT_NETWORK,
        ACT_COMMAND,
        ACT_POWER,
        ACT_WATCHDOG,
        ACT_SUPERVISOR,
    };
    size_t i;

    for (i = 0U; i < sizeof moved_actors / sizeof moved_actors[0]; ++i) {
        const ev_actor_module_descriptor_t *module = ev_actor_module_find(moved_actors[i]);
        assert(module != NULL);
        assert(module->handler_fn != NULL);
        assert(module->init_fn != NULL);
        assert(module->bind_fn != NULL);
    }
}

static void assert_runtime_builder_still_binds_moved_handler(void)
{
    ev_runtime_graph_t graph;
    ev_runtime_builder_t builder;
    ev_runtime_graph_stats_t stats;

    assert(ev_runtime_builder_init(&builder,
                                   &graph,
                                   EV_CAP_RTC | EV_CAP_I2C0 | EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS,
                                   EV_CAP_FAULTS | EV_CAP_METRICS | EV_CAP_TIMERS) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_RTC) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_FAULT) == EV_OK);
    assert(ev_runtime_builder_add_module(&builder, ACT_METRICS) == EV_OK);
    assert(ev_runtime_builder_bind_routes(&builder) == EV_OK);
    assert(ev_runtime_builder_build(&builder) == EV_OK);
    assert(ev_runtime_graph_get_runtime(&graph, ACT_RTC) != NULL);
    stats = ev_runtime_graph_stats(&graph);
    assert(stats.actor_count == 3U);
}

int main(void)
{
    assert_actor_file_layout();
    assert_module_handlers_survived_relocation();
    assert_runtime_builder_still_binds_moved_handler();
    return 0;
}
