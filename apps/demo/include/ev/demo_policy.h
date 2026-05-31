#ifndef EV_DEMO_POLICY_H
#define EV_DEMO_POLICY_H

#include <stdint.h>

#include "ev/demo_app.h"
#include "ev/power_actor.h"
#include "ev/watchdog_actor.h"

#ifdef __cplusplus
extern "C" {
#endif

ev_result_t ev_demo_app_sleep_quiescence_guard(void *ctx,
                                                uint64_t duration_us,
                                                ev_power_quiescence_report_t *out_report);
ev_result_t ev_demo_app_sleep_arm(void *ctx,
                                  uint64_t duration_us,
                                  ev_power_quiescence_report_t *out_report);
ev_result_t ev_demo_app_sleep_disarm(void *ctx);
ev_result_t ev_demo_app_watchdog_liveness(void *ctx, ev_watchdog_liveness_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif

#endif /* EV_DEMO_POLICY_H */
