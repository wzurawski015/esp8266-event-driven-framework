# Command security

Command security is split into:

- `ev_command_auth_port_t`,
- `ev_command_authorizer_t`,
- `ev_command_policy_t`,
- `ev_command_audit_record_t`.

The framework supports token authentication, capability-based authorization, rate-limit checks, audit counters, and events for accepted, rejected, and authentication-failed commands.
