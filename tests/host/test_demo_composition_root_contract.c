#include <assert.h>
#include <string.h>

#include "ev/demo_app.h"
#include "ev/demo_board_wiring.h"
#include "ev/demo_presentation.h"
#include "fakes/fake_clock_port.h"
#include "fakes/fake_log_port.h"

int main(void)
{
    fake_clock_port_t fake_clock;
    fake_log_port_t fake_log;
    ev_clock_port_t clock_port;
    ev_log_port_t log_port;
    ev_demo_app_config_t cfg;
    ev_demo_app_t app;
    ev_result_t rc;
    unsigned i;

    fake_clock_port_init(&fake_clock);
    fake_clock.mono_now_us = 1000000ULL;
    fake_clock.mono_step_us = 1000ULL;
    fake_clock_port_bind(&clock_port, &fake_clock);

    fake_log_port_init(&fake_log);
    fake_log_port_bind(&log_port, &fake_log);

    memset(&cfg, 0, sizeof(cfg));
    cfg.app_tag = "demo-composition-root";
    cfg.board_name = "host-default";
    cfg.clock_port = &clock_port;
    cfg.log_port = &log_port;
    cfg.board_profile = ev_demo_app_default_board_profile();

    assert(ev_demo_app_config_is_valid(&cfg));
    assert(ev_demo_app_render_oled_frame(NULL) == EV_ERR_INVALID_ARG);

    rc = ev_demo_app_init(&app, &cfg);
    assert(rc == EV_OK);
    assert(ev_demo_app_pending(&app) == 0U);

    rc = ev_demo_app_publish_boot(&app);
    assert(rc == EV_OK);
    assert(ev_demo_app_pending(&app) > 0U);

    for (i = 0U; i < 8U; ++i) {
        rc = ev_demo_app_poll(&app);
        assert((rc == EV_OK) || (rc == EV_ERR_PARTIAL));
        if (ev_demo_app_pending(&app) == 0U) {
            break;
        }
    }

    assert(ev_demo_app_pending(&app) == 0U);
    assert(ev_demo_app_stats(&app) != NULL);
    assert(fake_log.write_calls > 0U);
    return 0;
}
