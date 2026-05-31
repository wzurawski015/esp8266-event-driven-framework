#ifndef EV_SUPERVISOR_ACTOR_H
#define EV_SUPERVISOR_ACTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ev/compiler.h"
#include "ev/delivery.h"
#include "ev/msg.h"
#include "ev/result.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EV_SUPERVISOR_HW_MCP23008 0x00000001UL
#define EV_SUPERVISOR_HW_RTC 0x00000002UL
#define EV_SUPERVISOR_HW_OLED 0x00000004UL
#define EV_SUPERVISOR_HW_DS18B20 0x00000008UL
#define EV_SUPERVISOR_BOOT_SETTLE_TICKS 1U
#define EV_SUPERVISOR_REQUIRED_MASK (EV_SUPERVISOR_HW_MCP23008 | EV_SUPERVISOR_HW_RTC)
#define EV_SUPERVISOR_OPTIONAL_MASK (EV_SUPERVISOR_HW_OLED | EV_SUPERVISOR_HW_DS18B20)
#define EV_SUPERVISOR_KNOWN_MASK (EV_SUPERVISOR_REQUIRED_MASK | EV_SUPERVISOR_OPTIONAL_MASK)

typedef struct {
    uint32_t active_hardware_mask;
} ev_system_ready_payload_t;

EV_STATIC_ASSERT(sizeof(ev_system_ready_payload_t) <= EV_MSG_INLINE_CAPACITY,
                 "System ready payload must fit into one inline event payload");

typedef struct {
    ev_delivery_fn_t deliver;
    void *deliver_context;
    uint32_t active_hardware_mask;
    uint32_t observed_hardware_mask;
    uint32_t required_hardware_mask;
    uint32_t optional_hardware_mask;
    uint32_t known_hardware_mask;
    bool boot_observed;
    bool system_ready_published;
    uint32_t ticks_waited;
} ev_supervisor_actor_ctx_t;

ev_result_t ev_supervisor_actor_init(ev_supervisor_actor_ctx_t *ctx,
                                     ev_delivery_fn_t deliver,
                                     void *deliver_context);

ev_result_t ev_supervisor_actor_configure_hardware(ev_supervisor_actor_ctx_t *ctx,
                                                   uint32_t required_hardware_mask,
                                                   uint32_t optional_hardware_mask);

ev_result_t ev_supervisor_actor_handle(void *actor_context, const ev_msg_t *msg);

#ifdef __cplusplus
}
#endif

#endif /* EV_SUPERVISOR_ACTOR_H */
