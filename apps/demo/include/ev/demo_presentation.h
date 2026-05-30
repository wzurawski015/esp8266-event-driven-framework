#ifndef EV_DEMO_PRESENTATION_H
#define EV_DEMO_PRESENTATION_H

#include "ev/demo_app.h"

#ifdef __cplusplus
extern "C" {
#endif

ev_result_t ev_demo_app_render_oled_frame(ev_demo_app_actor_state_t *state);
ev_result_t ev_demo_app_handle_tick_for_oled(ev_demo_app_actor_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_PRESENTATION_H */
