# ADR-0001: Architectural foundation

- Status: Accepted
- Date: 2026-03-14

## Context

The project aims to become a long-lived framework for pure-C embedded applications on ESP8266.
A previous ESP32 prototype exists, but it is treated as an idea source, not as a golden reference.

The framework must support:

- deterministic event-driven execution,
- clean architecture boundaries,
- streaming-friendly dataflow,
- strong documentation and observability,
- academic clarity and production discipline.

## Decision

We adopt the following foundation:

1. **Active-object-inspired architecture** with explicit actor ownership boundaries.
2. **Static routing** driven from single sources of truth.
3. **Zero-heap hot path** after bootstrap.
4. **Pure C public interfaces** with explicit ownership contracts.
5. **Living documentation** generated from `config/*.def`.
6. **Docker-first reproducibility** for build, test, and documentation.
7. **Host-first validation** for core logic before hardware integration.

## Consequences

### Positive

- Stronger auditability.
- Easier reasoning about memory and timing.
- Better fit for constrained hardware.
- Documentation can stay close to the codebase.

### Trade-offs

- More up-front design discipline.
- Less ad-hoc flexibility.
- Higher pressure on generator quality and repository hygiene.

## Follow-up

Subsequent ADRs should define:

- memory ownership model,
- mailbox semantics,
- timing domains,
- ESP8266 SDK integration contract,
- BSP policy,
- diagnostics and tracing policy.
