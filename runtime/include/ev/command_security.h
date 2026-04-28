#ifndef EV_COMMAND_SECURITY_H
#define EV_COMMAND_SECURITY_H

#include <stdint.h>
#include "ev/result.h"

#define EV_COMMAND_CAP_LED 0x00000001UL
#define EV_COMMAND_CAP_DISPLAY 0x00000002UL
#define EV_COMMAND_CAP_SLEEP 0x00000004UL
#define EV_COMMAND_CAP_NETWORK 0x00000008UL

typedef struct {
    const char *expected_token;
} ev_command_auth_port_t;

typedef struct {
    uint32_t allowed_capabilities;
} ev_command_authorizer_t;

typedef struct {
    uint32_t required_capability;
    uint32_t min_interval_ms;
    uint32_t last_accepted_ms;
    uint8_t replay_protection_enabled;
} ev_command_policy_t;

typedef struct {
    uint32_t accepted;
    uint32_t rejected;
    uint32_t auth_failed;
    uint32_t authz_failed;
    uint32_t rate_limited;
    uint32_t replay_rejected;
} ev_command_audit_record_t;

ev_result_t ev_command_authenticate(const ev_command_auth_port_t *auth, const char *token, ev_command_audit_record_t *audit);
ev_result_t ev_command_authorize(const ev_command_authorizer_t *authorizer, const ev_command_policy_t *policy, uint32_t now_ms, ev_command_audit_record_t *audit);

#endif
