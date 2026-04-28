#include "ev/command_security.h"

static int ev_str_equal(const char *a, const char *b)
{
    if ((a == 0) || (b == 0)) {
        return 0;
    }
    while ((*a != '\0') && (*b != '\0')) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return ((*a == '\0') && (*b == '\0')) ? 1 : 0;
}

ev_result_t ev_command_authenticate(const ev_command_auth_port_t *auth, const char *token, ev_command_audit_record_t *audit)
{
    if ((auth == 0) || (auth->expected_token == 0) || (token == 0)) {
        if (audit != 0) {
            audit->auth_failed++;
            audit->rejected++;
        }
        return EV_ERR_AUTH;
    }
    if (ev_str_equal(auth->expected_token, token) == 0) {
        if (audit != 0) {
            audit->auth_failed++;
            audit->rejected++;
        }
        return EV_ERR_AUTH;
    }
    return EV_OK;
}

ev_result_t ev_command_authorize(const ev_command_authorizer_t *authorizer, const ev_command_policy_t *policy, uint32_t now_ms, ev_command_audit_record_t *audit)
{
    if ((authorizer == 0) || (policy == 0)) {
        if (audit != 0) {
            audit->rejected++;
        }
        return EV_ERR_INVALID_ARG;
    }
    if ((authorizer->allowed_capabilities & policy->required_capability) != policy->required_capability) {
        if (audit != 0) {
            audit->authz_failed++;
            audit->rejected++;
        }
        return EV_ERR_AUTH;
    }
    if ((policy->min_interval_ms != 0U) && ((uint32_t)(now_ms - policy->last_accepted_ms) < policy->min_interval_ms)) {
        if (audit != 0) {
            audit->rate_limited++;
            audit->rejected++;
        }
        return EV_ERR_POLICY;
    }
    if (audit != 0) {
        audit->accepted++;
    }
    return EV_OK;
}
