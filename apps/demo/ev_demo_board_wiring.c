#include "ev/demo_board_wiring.h"

#include <string.h>

#include "ev/capabilities.h"
#include "ev/demo_internal.h"
#include "ev/demo_policy.h"
#include "ev/demo_runtime_instances.h"
#include "ev/runtime_graph.h"
#include "ev/runtime_graph_timers.h"

#define EV_DEMO_APP_FAST_TICK_MS 100U
#define EV_DEMO_APP_MIN_WDT_TIMEOUT_MS 1000U
#define EV_DEMO_APP_MAX_WDT_TIMEOUT_MS 60000U

static const ev_demo_app_board_profile_t k_ev_demo_app_default_board_profile = {
    .capabilities_mask = 0U,
    .hardware_present_mask = 0U,
    .supervisor_required_mask = 0U,
    .supervisor_optional_mask = 0U,
    .i2c_port_num = EV_I2C_PORT_NUM_0,
    .rtc_sqw_line_id = 0U,
    .mcp23008_addr_7bit = 0U,
    .rtc_addr_7bit = 0U,
    .oled_addr_7bit = 0U,
    .oled_controller = EV_OLED_CONTROLLER_SSD1306,
    .watchdog_timeout_ms = 0U,
    .remote_command_token = "",
    .remote_command_capabilities = 0U,
};

const ev_demo_app_board_profile_t *ev_demo_app_default_board_profile(void)
{
    return &k_ev_demo_app_default_board_profile;
}

static bool ev_demo_app_hw_mask_valid(uint32_t hw_mask)
{
    return (hw_mask & (uint32_t)(~EV_SUPERVISOR_KNOWN_MASK)) == 0U;
}

static bool ev_demo_app_profile_is_valid(const ev_demo_app_board_profile_t *profile)
{
    uint32_t supervised_mask;

    if (profile == NULL) {
        return false;
    }

    supervised_mask = profile->supervisor_required_mask | profile->supervisor_optional_mask;
    if (!ev_demo_app_hw_mask_valid(profile->hardware_present_mask) ||
        !ev_demo_app_hw_mask_valid(profile->supervisor_required_mask) ||
        !ev_demo_app_hw_mask_valid(profile->supervisor_optional_mask)) {
        return false;
    }
    if ((profile->supervisor_required_mask & profile->supervisor_optional_mask) != 0U) {
        return false;
    }
    if ((supervised_mask & (uint32_t)(~profile->hardware_present_mask)) != 0U) {
        return false;
    }
    if ((profile->hardware_present_mask & (EV_SUPERVISOR_HW_MCP23008 |
                                           EV_SUPERVISOR_HW_RTC |
                                           EV_SUPERVISOR_HW_OLED)) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_I2C0) == 0U) {
            return false;
        }
    }
    if ((profile->hardware_present_mask & EV_SUPERVISOR_HW_DS18B20) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_ONEWIRE0) == 0U) {
            return false;
        }
    }
    if ((profile->hardware_present_mask & EV_SUPERVISOR_HW_RTC) != 0U) {
        if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_GPIO_IRQ) == 0U) {
            return false;
        }
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_MCP23008) != 0U) &&
        ((profile->mcp23008_addr_7bit == 0U) || (profile->mcp23008_addr_7bit > 0x7FU))) {
        return false;
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_RTC) != 0U) &&
        ((profile->rtc_addr_7bit == 0U) || (profile->rtc_addr_7bit > 0x7FU))) {
        return false;
    }
    if (((profile->hardware_present_mask & EV_SUPERVISOR_HW_OLED) != 0U) &&
        ((profile->oled_addr_7bit == 0U) || (profile->oled_addr_7bit > 0x7FU))) {
        return false;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        if ((profile->watchdog_timeout_ms < EV_DEMO_APP_MIN_WDT_TIMEOUT_MS) ||
            (profile->watchdog_timeout_ms > EV_DEMO_APP_MAX_WDT_TIMEOUT_MS)) {
            return false;
        }
    }

    return true;
}

bool ev_demo_app_profile_has_hardware(const ev_demo_app_t *app, uint32_t hw_mask)
{
    return (app != NULL) && ((app->board_profile.hardware_present_mask & hw_mask) != 0U);
}

ev_result_t ev_demo_app_init_publish_ports(ev_demo_app_t *app)
{
    size_t i;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        ev_result_t rc = ev_actor_publish_port_init(&app->publish_ports[i], &app->graph, (ev_actor_id_t)i);
        if (rc != EV_OK) {
            return rc;
        }
    }
    return EV_OK;
}

void ev_demo_app_record_publish_port_stats(ev_demo_app_t *app)
{
    size_t i;
    if (app == NULL) {
        return;
    }
    for (i = 0U; i < (size_t)EV_ACTOR_COUNT; ++i) {
        const ev_actor_publish_port_stats_t *stats = ev_actor_publish_port_stats(&app->publish_ports[i]);
        if (stats == NULL) {
            continue;
        }
        if (stats->optional_disabled_routes > app->publish_port_disabled_consumed[i]) {
            app->stats.disabled_route_deliveries += stats->optional_disabled_routes - app->publish_port_disabled_consumed[i];
            app->publish_port_disabled_consumed[i] = stats->optional_disabled_routes;
        }
        if (stats->optional_disabled_watchdog_routes > app->publish_port_disabled_watchdog_consumed[i]) {
            app->stats.watchdog_disabled_route_deliveries += stats->optional_disabled_watchdog_routes - app->publish_port_disabled_watchdog_consumed[i];
            app->publish_port_disabled_watchdog_consumed[i] = stats->optional_disabled_watchdog_routes;
        }
        if (stats->optional_disabled_network_routes > app->publish_port_disabled_network_consumed[i]) {
            app->stats.network_disabled_route_deliveries += stats->optional_disabled_network_routes - app->publish_port_disabled_network_consumed[i];
            app->publish_port_disabled_network_consumed[i] = stats->optional_disabled_network_routes;
        }
    }
}

static bool ev_demo_app_i2c_port_valid(const ev_i2c_port_t *port)
{
    return (port != NULL) && (port->write_stream != NULL) && (port->read_stream != NULL) &&
           (port->write_regs != NULL) && (port->read_regs != NULL);
}

static bool ev_demo_app_onewire_port_valid(const ev_onewire_port_t *port)
{
    return (port != NULL) && (port->reset != NULL) &&
           (port->write_byte != NULL) && (port->read_byte != NULL);
}

static bool ev_demo_app_irq_port_valid(const ev_irq_port_t *port)
{
    return (port != NULL) && (port->pop != NULL) && (port->enable != NULL);
}

static bool ev_demo_app_wdt_port_valid(const ev_wdt_port_t *port)
{
    return (port != NULL) && (port->enable != NULL) && (port->feed != NULL);
}

static bool ev_demo_app_net_port_valid(const ev_net_port_t *port)
{
    return (port != NULL) && (port->poll_ingress != NULL) &&
           (port->publish_mqtt != NULL) && (port->get_stats != NULL);
}

bool ev_demo_app_config_is_valid(const ev_demo_app_config_t *cfg)
{
    const ev_demo_app_board_profile_t *profile;
    uint32_t hw_mask;

    if ((cfg == NULL) || (cfg->app_tag == NULL) || (cfg->board_name == NULL) ||
        (cfg->clock_port == NULL) || (cfg->clock_port->mono_now_us == NULL) ||
        (cfg->log_port == NULL) || (cfg->log_port->write == NULL) ||
        ((cfg->system_port != NULL) && (cfg->system_port->deep_sleep == NULL))) {
        return false;
    }

    profile = (cfg->board_profile != NULL) ? cfg->board_profile : ev_demo_app_default_board_profile();
    if (!ev_demo_app_profile_is_valid(profile)) {
        return false;
    }

    hw_mask = profile->hardware_present_mask;
    if ((hw_mask & (EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC | EV_SUPERVISOR_HW_OLED)) != 0U) {
        if (!ev_demo_app_i2c_port_valid(cfg->i2c_port)) {
            return false;
        }
    }
    if ((hw_mask & EV_SUPERVISOR_HW_DS18B20) != 0U) {
        if (!ev_demo_app_onewire_port_valid(cfg->onewire_port)) {
            return false;
        }
    }
    if ((hw_mask & EV_SUPERVISOR_HW_RTC) != 0U) {
        if (!ev_demo_app_irq_port_valid(cfg->irq_port)) {
            return false;
        }
    } else if ((cfg->irq_port != NULL) && ((cfg->irq_port->pop == NULL) || (cfg->irq_port->enable == NULL))) {
        return false;
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        if (!ev_demo_app_wdt_port_valid(cfg->wdt_port)) {
            return false;
        }
    }
    if ((profile->capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) {
        if (!ev_demo_app_net_port_valid(cfg->net_port)) {
            return false;
        }
    }

    return true;
}


static ev_capability_mask_t ev_demo_app_runtime_board_capabilities(const ev_demo_app_t *app)
{
    ev_capability_mask_t caps = EV_CAP_PANEL | EV_CAP_METRICS | EV_CAP_FAULTS |
                               EV_CAP_TRACE | EV_CAP_POWER_POLICY | EV_CAP_REMOTE_COMMANDS;
    if (app == NULL) {
        return caps;
    }
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_I2C0) != 0U) caps |= EV_CAP_I2C0;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_ONEWIRE0) != 0U) caps |= EV_CAP_ONEWIRE0;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_GPIO_IRQ) != 0U) caps |= EV_CAP_GPIO_IRQ;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) caps |= EV_CAP_WDT;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) caps |= EV_CAP_NET;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_RTC)) caps |= EV_CAP_RTC;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_OLED)) caps |= EV_CAP_OLED;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_DS18B20)) caps |= EV_CAP_DS18B20;
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_MCP23008)) caps |= EV_CAP_MCP23008;
    return caps;
}

static ev_capability_mask_t ev_demo_app_runtime_capabilities(void)
{
    return EV_CAP_TIMERS | EV_CAP_METRICS | EV_CAP_FAULTS | EV_CAP_TRACE | EV_CAP_POWER_POLICY;
}

static ev_runtime_ports_t ev_demo_app_runtime_ports(ev_demo_app_t *app)
{
    ev_runtime_ports_t ports;
    memset(&ports, 0, sizeof(ports));
    if (app != NULL) {
        ports.clock = app->clock_port;
        ports.log = app->log_port;
        ports.irq = app->irq_port;
        ports.system = app->system_port;
        ports.wdt = app->wdt_port;
        ports.net = app->net_port;
    }
    return ports;
}

static ev_runtime_board_profile_t ev_demo_app_runtime_board_profile(const ev_demo_app_t *app)
{
    ev_runtime_board_profile_t profile;
    memset(&profile, 0, sizeof(profile));
    if (app != NULL) {
        profile.board_name = app->board_name;
        profile.configured_capabilities = ev_demo_app_runtime_board_capabilities(app);
        profile.active_capabilities = profile.configured_capabilities;
        profile.hardware_present = profile.configured_capabilities;
        profile.required_hardware = app->board_profile.supervisor_required_mask;
        profile.optional_hardware = app->board_profile.supervisor_optional_mask;
        profile.bsp_private = &app->board_profile;
    }
    return profile;
}

ev_result_t ev_demo_app_configure_runtime(ev_demo_app_t *app, const ev_demo_app_config_t *cfg)
{
    ev_result_t rc;
    uint32_t now_ms;
    ev_i2c_port_t *active_i2c;
    ev_onewire_port_t *active_onewire;

    if ((app == NULL) || (cfg == NULL)) {
        return EV_ERR_INVALID_ARG;
    }

    active_i2c = ((app->board_profile.hardware_present_mask &
                   (EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC | EV_SUPERVISOR_HW_OLED)) != 0U)
                     ? cfg->i2c_port
                     : NULL;
    active_onewire = ((app->board_profile.hardware_present_mask & EV_SUPERVISOR_HW_DS18B20) != 0U)
                       ? cfg->onewire_port
                       : NULL;

    rc = ev_demo_app_now_ms(app, &now_ms);
    if (rc != EV_OK) return rc;
    rc = ev_demo_app_init_publish_ports(app);
    if (rc != EV_OK) return rc;

    rc = ev_panel_actor_init(&app->panel_ctx, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_PANEL]);
    if (rc != EV_OK) return rc;
    rc = ev_supervisor_actor_init(&app->supervisor_ctx, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_SUPERVISOR]);
    if (rc != EV_OK) return rc;
    rc = ev_supervisor_actor_configure_hardware(&app->supervisor_ctx,
                                                app->board_profile.supervisor_required_mask,
                                                app->board_profile.supervisor_optional_mask);
    if (rc != EV_OK) return rc;
    rc = ev_power_actor_init(&app->power_ctx, app->system_port, app->log_port, app->app_tag);
    if (rc != EV_OK) return rc;
    rc = ev_command_actor_init(&app->command_ctx, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_COMMAND], app->board_profile.remote_command_token, app->board_profile.remote_command_capabilities);
    if (rc != EV_OK) return rc;
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U) {
        rc = ev_watchdog_actor_init(&app->watchdog_ctx, app->wdt_port, app->board_profile.watchdog_timeout_ms, ev_demo_app_watchdog_liveness, app);
        if (rc != EV_OK) return rc;
    }
    if ((app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U) {
        if ((app->net_port == NULL) || (app->net_port->init == NULL) || (app->net_port->start == NULL)) {
            return EV_ERR_INVALID_ARG;
        }
        rc = app->net_port->init(app->net_port->ctx);
        if (rc != EV_OK) return rc;
        rc = app->net_port->start(app->net_port->ctx);
        if (rc != EV_OK) return rc;
        rc = ev_network_actor_init(&app->network_ctx, app->net_port);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_MCP23008)) {
        rc = ev_mcp23008_actor_init(&app->mcp23008_ctx, active_i2c, app->board_profile.i2c_port_num, app->board_profile.mcp23008_addr_7bit, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_MCP23008]);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_RTC)) {
        rc = ev_rtc_actor_init(&app->rtc_ctx, active_i2c, app->irq_port, app->board_profile.i2c_port_num, app->board_profile.rtc_addr_7bit, app->board_profile.rtc_sqw_line_id, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_RTC]);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_DS18B20)) {
        rc = ev_ds18b20_actor_init(&app->ds18b20_ctx, active_onewire, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_DS18B20]);
        if (rc != EV_OK) return rc;
    }
    if (ev_demo_app_profile_has_hardware(app, EV_SUPERVISOR_HW_OLED)) {
        rc = ev_oled_actor_init(&app->oled_ctx, active_i2c, app->board_profile.i2c_port_num, app->board_profile.oled_addr_7bit, app->board_profile.oled_controller, ev_actor_publish_port_delivery_adapter, &app->publish_ports[ACT_OLED]);
        if (rc != EV_OK) return rc;
    }
    rc = ev_demo_app_build_runtime_graph(app);
    if (rc != EV_OK) return rc;
    rc = ev_demo_app_schedule_standard_timers(app, now_ms);
    if (rc != EV_OK) return rc;

    rc = ev_power_actor_set_quiescence_guard(&app->power_ctx, ev_demo_app_sleep_quiescence_guard, app);
    if (rc != EV_OK) return rc;

    rc = ev_power_actor_set_sleep_arming(&app->power_ctx,
                                         ev_demo_app_sleep_arm,
                                         ev_demo_app_sleep_disarm,
                                         app);
    if (rc != EV_OK) return rc;

    return ev_lease_pool_init(&app->lease_pool,
                              app->lease_slots,
                              app->lease_storage,
                              EV_DEMO_APP_LEASE_SLOTS,
                              EV_DEMO_APP_LEASE_SLOT_BYTES);
}


ev_result_t ev_demo_app_build_runtime_graph(ev_demo_app_t *app)
{
    ev_runtime_builder_t builder;
    ev_runtime_ports_t ports;
    ev_runtime_board_profile_t profile;
    ev_actor_instance_descriptor_t instances[EV_ACTOR_COUNT];
    size_t count = 0U;
    size_t i;
    ev_result_t rc;

    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_runtime_builder_init(&builder,
                                 &app->graph,
                                 ev_demo_app_runtime_board_capabilities(app),
                                 ev_demo_app_runtime_capabilities());
    if (rc != EV_OK) return rc;
    ports = ev_demo_app_runtime_ports(app);
    profile = ev_demo_app_runtime_board_profile(app);
    rc = ev_runtime_builder_set_ports(&builder, &ports);
    if (rc != EV_OK) return rc;
    rc = ev_runtime_builder_set_board_profile(&builder, &profile);
    if (rc != EV_OK) return rc;
    rc = ev_demo_runtime_instances_init(app, instances, EV_ACTOR_COUNT, &count);
    if (rc != EV_OK) return rc;
    for (i = 0U; i < count; ++i) {
        rc = ev_runtime_builder_add_instance(&builder, &instances[i]);
        if (rc != EV_OK) return rc;
    }
    rc = ev_runtime_builder_bind_routes(&builder);
    if (rc != EV_OK) return rc;
    return ev_runtime_builder_build(&builder);
}

ev_result_t ev_demo_app_schedule_standard_timers(ev_demo_app_t *app, uint32_t now_ms)
{
    ev_result_t rc;
    if (app == NULL) {
        return EV_ERR_INVALID_ARG;
    }
    rc = ev_runtime_graph_schedule_periodic(&app->graph,
                                       now_ms,
                                       EV_DEMO_APP_FAST_TICK_MS,
                                       ACT_RUNTIME,
                                       EV_TICK_100MS,
                                       0U,
                                       &app->tick_100ms_token);
    if (rc != EV_OK) return rc;
    rc = ev_runtime_graph_schedule_periodic(&app->graph,
                                       now_ms,
                                       app->tick_period_ms,
                                       ACT_RUNTIME,
                                       EV_TICK_1S,
                                       0U,
                                       &app->tick_1s_token);
    if (rc == EV_OK) {
        app->standard_timers_scheduled = true;
    }
    return rc;
}
