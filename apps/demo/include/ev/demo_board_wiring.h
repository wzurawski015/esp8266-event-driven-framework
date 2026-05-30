#ifndef EV_DEMO_BOARD_WIRING_H
#define EV_DEMO_BOARD_WIRING_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/demo_app.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ev_demo_app_config_is_valid(const ev_demo_app_config_t *cfg);
bool ev_demo_app_profile_has_hardware(const ev_demo_app_t *app, uint32_t hw_mask);
ev_result_t ev_demo_app_init_publish_ports(ev_demo_app_t *app);
ev_result_t ev_demo_app_configure_runtime(ev_demo_app_t *app, const ev_demo_app_config_t *cfg);
ev_result_t ev_demo_app_build_runtime_graph(ev_demo_app_t *app);
ev_result_t ev_demo_app_schedule_standard_timers(ev_demo_app_t *app, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_BOARD_WIRING_H */
