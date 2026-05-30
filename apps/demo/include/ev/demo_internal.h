#ifndef EV_DEMO_INTERNAL_H
#define EV_DEMO_INTERNAL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ev/demo_app.h"
#include "ev/delivery_service.h"
#include "ev/msg.h"

#ifdef __cplusplus
extern "C" {
#endif

void ev_demo_app_logf(ev_demo_app_t *app, ev_log_level_t level, const char *fmt, ...);
ev_result_t ev_demo_app_now_ms(ev_demo_app_t *app, uint32_t *out_now_ms);
ev_result_t ev_demo_app_publish_owned(ev_demo_app_t *app, ev_msg_t *msg);
ev_result_t ev_demo_app_publish_panel_led_command(ev_demo_app_t *app, uint8_t value_mask, uint8_t valid_mask);
ev_result_t ev_demo_app_publish_oled_scene_commit(ev_demo_app_t *app, const ev_oled_scene_t *scene);
bool ev_demo_app_hardware_active(const ev_demo_app_actor_state_t *state, uint32_t hw_mask);
void ev_demo_app_record_publish_port_stats(ev_demo_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_INTERNAL_H */
