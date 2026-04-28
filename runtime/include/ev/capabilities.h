#ifndef EV_CAPABILITIES_H
#define EV_CAPABILITIES_H

#include <stdint.h>

typedef uint32_t ev_capability_mask_t;

#define EV_CAP_NONE 0UL
#define EV_CAP_ALL_KNOWN (     EV_CAP_I2C0 | EV_CAP_ONEWIRE0 | EV_CAP_GPIO_IRQ | EV_CAP_WDT | EV_CAP_NET |     EV_CAP_REMOTE_COMMANDS | EV_CAP_RTC | EV_CAP_OLED | EV_CAP_DS18B20 |     EV_CAP_MCP23008 | EV_CAP_PANEL | EV_CAP_FAULTS | EV_CAP_METRICS |     EV_CAP_TRACE | EV_CAP_TIMERS | EV_CAP_POWER_POLICY)

enum {
#define EV_CAPABILITY(name, bit, summary) name = bit,
#include "capabilities.def"
#undef EV_CAPABILITY
};

typedef struct {
    ev_capability_mask_t configured;
    ev_capability_mask_t active;
    ev_capability_mask_t observed;
} ev_board_capability_snapshot_t;

typedef struct {
    ev_capability_mask_t configured;
    ev_capability_mask_t active;
    ev_capability_mask_t required;
} ev_runtime_capability_snapshot_t;

typedef struct {
    ev_capability_mask_t wake_sources;
    ev_capability_mask_t power_domains;
    ev_capability_mask_t blockers;
} ev_power_capability_snapshot_t;

#endif
