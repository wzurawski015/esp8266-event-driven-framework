#include "ev/demo_runtime_instances.h"

#include <string.h>

#include "ev/actor_module.h"
#include "ev/capabilities.h"

static int ev_demo_runtime_has_hardware(const ev_demo_app_t *app, uint32_t hw_mask)
{
    return (app != NULL) && ((app->board_profile.hardware_present_mask & hw_mask) != 0U);
}

static int ev_demo_runtime_actor_enabled(const ev_demo_app_t *app, ev_actor_id_t actor_id)
{
    if (app == NULL) {
        return 0;
    }
    switch (actor_id) {
    case ACT_MCP23008:
        return ev_demo_runtime_has_hardware(app, EV_SUPERVISOR_HW_MCP23008);
    case ACT_RTC:
        return ev_demo_runtime_has_hardware(app, EV_SUPERVISOR_HW_RTC);
    case ACT_DS18B20:
        return ev_demo_runtime_has_hardware(app, EV_SUPERVISOR_HW_DS18B20);
    case ACT_OLED:
        return ev_demo_runtime_has_hardware(app, EV_SUPERVISOR_HW_OLED);
    case ACT_WATCHDOG:
        return (app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_WDT) != 0U;
    case ACT_NETWORK:
        return (app->board_profile.capabilities_mask & EV_DEMO_APP_BOARD_CAP_NET) != 0U;
    default:
        return 1;
    }
}

static void *ev_demo_runtime_context_for(ev_demo_app_t *app, ev_actor_id_t actor_id)
{
    if (app == NULL) {
        return NULL;
    }
    switch (actor_id) {
    case ACT_RUNTIME:
        return app;
    case ACT_APP:
        return &app->app_actor;
    case ACT_DIAG:
        return &app->diag_actor;
    case ACT_PANEL:
        return &app->panel_ctx;
    case ACT_RTC:
        return &app->rtc_ctx;
    case ACT_MCP23008:
        return &app->mcp23008_ctx;
    case ACT_DS18B20:
        return &app->ds18b20_ctx;
    case ACT_OLED:
        return &app->oled_ctx;
    case ACT_SUPERVISOR:
        return &app->supervisor_ctx;
    case ACT_POWER:
        return &app->power_ctx;
    case ACT_WATCHDOG:
        return &app->watchdog_ctx;
    case ACT_NETWORK:
        return &app->network_ctx;
    case ACT_COMMAND:
        return &app->command_ctx;
    default:
        return NULL;
    }
}

static size_t ev_demo_runtime_context_size_for(ev_actor_id_t actor_id)
{
    switch (actor_id) {
    case ACT_RUNTIME:
        return sizeof(ev_demo_app_t);
    case ACT_APP:
        return sizeof(ev_demo_app_actor_state_t);
    case ACT_DIAG:
        return sizeof(ev_demo_diag_actor_state_t);
    case ACT_PANEL:
        return sizeof(ev_panel_actor_ctx_t);
    case ACT_RTC:
        return sizeof(ev_rtc_actor_ctx_t);
    case ACT_MCP23008:
        return sizeof(ev_mcp23008_actor_ctx_t);
    case ACT_DS18B20:
        return sizeof(ev_ds18b20_actor_ctx_t);
    case ACT_OLED:
        return sizeof(ev_oled_actor_ctx_t);
    case ACT_SUPERVISOR:
        return sizeof(ev_supervisor_actor_ctx_t);
    case ACT_POWER:
        return sizeof(ev_power_actor_ctx_t);
    case ACT_WATCHDOG:
        return sizeof(ev_watchdog_actor_ctx_t);
    case ACT_NETWORK:
        return sizeof(ev_network_actor_ctx_t);
    case ACT_COMMAND:
        return sizeof(ev_command_actor_ctx_t);
    default:
        return 0U;
    }
}

static ev_actor_handler_fn_t ev_demo_runtime_handler_for(ev_actor_id_t actor_id)
{
    switch (actor_id) {
    case ACT_RUNTIME:
        return ev_demo_runtime_actor_handle;
    case ACT_APP:
        return ev_demo_app_actor_handle;
    case ACT_DIAG:
        return ev_demo_diag_actor_handle;
    case ACT_PANEL:
        return ev_panel_actor_handle;
    case ACT_RTC:
        return ev_rtc_actor_handle;
    case ACT_MCP23008:
        return ev_mcp23008_actor_handle;
    case ACT_DS18B20:
        return ev_ds18b20_actor_handle;
    case ACT_OLED:
        return ev_oled_actor_handle;
    case ACT_SUPERVISOR:
        return ev_supervisor_actor_handle;
    case ACT_POWER:
        return ev_power_actor_handle;
    case ACT_WATCHDOG:
        return ev_watchdog_actor_handle;
    case ACT_NETWORK:
        return ev_network_actor_handle;
    case ACT_COMMAND:
        return ev_command_actor_handle;
    default:
        return NULL;
    }
}

static ev_actor_quiescence_fn_t ev_demo_runtime_quiescence_for(ev_actor_id_t actor_id)
{
    switch (actor_id) {
    case ACT_OLED:
        return ev_demo_oled_quiescence;
    case ACT_DS18B20:
        return ev_demo_ds18b20_quiescence;
    default:
        return NULL;
    }
}

static ev_result_t ev_demo_runtime_add_instance(ev_demo_app_t *app,
                                                ev_actor_id_t actor_id,
                                                ev_actor_instance_descriptor_t *out_instances,
                                                size_t capacity,
                                                size_t *io_count)
{
    ev_actor_instance_descriptor_t *instance;
    const ev_actor_module_descriptor_t *module;

    if ((app == NULL) || (out_instances == NULL) || (io_count == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (!ev_demo_runtime_actor_enabled(app, actor_id)) {
        return EV_OK;
    }
    if (*io_count >= capacity) {
        return EV_ERR_FULL;
    }
    module = ev_actor_module_find(actor_id);
    if (module == NULL) {
        return EV_ERR_NOT_FOUND;
    }
    instance = &out_instances[*io_count];
    (void)memset(instance, 0, sizeof(*instance));
    instance->actor_id = actor_id;
    instance->module = module;
    instance->actor_context = ev_demo_runtime_context_for(app, actor_id);
    instance->actor_context_size = ev_demo_runtime_context_size_for(actor_id);
    instance->handler_fn = ev_demo_runtime_handler_for(actor_id);
    instance->quiescence_fn = ev_demo_runtime_quiescence_for(actor_id);
    instance->required_capabilities = module->required_board_capabilities;
    instance->optional_capabilities = 0U;
    instance->user = app;
    if ((instance->actor_context == NULL) || (instance->handler_fn == NULL)) {
        return EV_ERR_CONTRACT;
    }
    ++(*io_count);
    return EV_OK;
}

size_t ev_demo_runtime_instance_count(const ev_demo_app_t *app)
{
    static const ev_actor_id_t actors[] = {
        ACT_RUNTIME, ACT_APP, ACT_DIAG, ACT_PANEL, ACT_RTC, ACT_MCP23008, ACT_DS18B20,
        ACT_OLED, ACT_SUPERVISOR, ACT_POWER, ACT_WATCHDOG, ACT_NETWORK, ACT_COMMAND,
    };
    size_t i;
    size_t count = 0U;
    for (i = 0U; i < (sizeof(actors) / sizeof(actors[0])); ++i) {
        if (ev_demo_runtime_actor_enabled(app, actors[i])) {
            ++count;
        }
    }
    return count;
}

ev_result_t ev_demo_runtime_instances_init(ev_demo_app_t *app,
                                          ev_actor_instance_descriptor_t *out_instances,
                                          size_t capacity,
                                          size_t *out_count)
{
    static const ev_actor_id_t actors[] = {
        ACT_RUNTIME, ACT_APP, ACT_DIAG, ACT_PANEL, ACT_RTC, ACT_MCP23008, ACT_DS18B20,
        ACT_OLED, ACT_SUPERVISOR, ACT_POWER, ACT_WATCHDOG, ACT_NETWORK, ACT_COMMAND,
    };
    size_t i;
    size_t count = 0U;
    ev_result_t rc;

    if ((app == NULL) || (out_instances == NULL) || (out_count == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    for (i = 0U; i < (sizeof(actors) / sizeof(actors[0])); ++i) {
        rc = ev_demo_runtime_add_instance(app, actors[i], out_instances, capacity, &count);
        if (rc != EV_OK) {
            return rc;
        }
    }
    *out_count = count;
    return EV_OK;
}

ev_result_t ev_demo_oled_quiescence(void *actor_context, ev_quiescence_report_t *report)
{
    const ev_oled_actor_ctx_t *ctx = (const ev_oled_actor_ctx_t *)actor_context;
    if ((ctx == NULL) || (report == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ctx->pending_flush) {
        report->sleep_blocker_actor_mask |= (1UL << ACT_OLED);
    }
    return EV_OK;
}

ev_result_t ev_demo_ds18b20_quiescence(void *actor_context, ev_quiescence_report_t *report)
{
    const ev_ds18b20_actor_ctx_t *ctx = (const ev_ds18b20_actor_ctx_t *)actor_context;
    if ((ctx == NULL) || (report == NULL)) {
        return EV_ERR_INVALID_ARG;
    }
    if (ctx->conversion_pending) {
        report->sleep_blocker_actor_mask |= (1UL << ACT_DS18B20);
    }
    return EV_OK;
}
