#ifndef EV_RUNTIME_BOARD_PROFILE_H
#define EV_RUNTIME_BOARD_PROFILE_H

#include <stdint.h>

#include "ev/capabilities.h"

/**
 * @brief Framework-level runtime view of the selected board/BSP profile.
 */
typedef struct {
    const char *board_name;
    ev_capability_mask_t configured_capabilities;
    ev_capability_mask_t active_capabilities;
    ev_capability_mask_t observed_capabilities;
    ev_capability_mask_t hardware_present;
    ev_capability_mask_t required_hardware;
    ev_capability_mask_t optional_hardware;
    uint32_t policy_flags;
    const void *bsp_private;
} ev_runtime_board_profile_t;

#endif /* EV_RUNTIME_BOARD_PROFILE_H */
