#include <assert.h>
#include <stdint.h>

#include "ev/ingress_service.h"

int main(void)
{
    ev_ingress_service_t ingress;
    ev_msg_t msg = EV_MSG_INITIALIZER;
    ev_msg_t out = EV_MSG_INITIALIZER;
    size_t i;

    ev_ingress_service_init(&ingress);
    assert(ev_msg_init_publish(&msg, EV_TICK_1S, ACT_APP) == EV_OK);
    for (i = 0U; i < EV_INGRESS_CAPACITY; ++i) {
        assert(ev_ingress_push(&ingress, &msg) == EV_OK);
    }
    assert(ev_ingress_pending(&ingress) == EV_INGRESS_CAPACITY);
    assert(ingress.high_water == EV_INGRESS_CAPACITY);
    assert(ev_ingress_push(&ingress, &msg) == EV_ERR_FULL);
    assert(ingress.dropped == 1U);
    for (i = 0U; i < EV_INGRESS_CAPACITY; ++i) {
        assert(ev_ingress_pop(&ingress, &out) == EV_OK);
    }
    assert(ev_ingress_pop(&ingress, &out) == EV_ERR_EMPTY);

    ingress.write_seq = UINT32_MAX - 2U;
    ingress.read_seq = UINT32_MAX - 2U;
    for (i = 0U; i < 4U; ++i) {
        assert(ev_ingress_push(&ingress, &msg) == EV_OK);
    }
    assert(ev_ingress_pending(&ingress) == 4U);
    for (i = 0U; i < 4U; ++i) {
        assert(ev_ingress_pop(&ingress, &out) == EV_OK);
    }
    assert(ev_ingress_pending(&ingress) == 0U);
    return 0;
}
