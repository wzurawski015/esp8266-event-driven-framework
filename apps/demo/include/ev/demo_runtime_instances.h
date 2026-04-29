#ifndef EV_DEMO_RUNTIME_INSTANCES_H
#define EV_DEMO_RUNTIME_INSTANCES_H

#include <stddef.h>

#include "ev/actor_instance.h"
#include "ev/demo_app.h"

#ifdef __cplusplus
extern "C" {
#endif

size_t ev_demo_runtime_instance_count(const ev_demo_app_t *app);

ev_result_t ev_demo_runtime_instances_init(ev_demo_app_t *app,
                                          ev_actor_instance_descriptor_t *out_instances,
                                          size_t capacity,
                                          size_t *out_count);

ev_result_t ev_demo_app_actor_handle(void *actor_context, const ev_msg_t *msg);
ev_result_t ev_demo_diag_actor_handle(void *actor_context, const ev_msg_t *msg);
ev_result_t ev_demo_runtime_actor_handle(void *actor_context, const ev_msg_t *msg);

ev_result_t ev_demo_oled_quiescence(void *actor_context, ev_quiescence_report_t *report);
ev_result_t ev_demo_ds18b20_quiescence(void *actor_context, ev_quiescence_report_t *report);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_RUNTIME_INSTANCES_H */
