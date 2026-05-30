# Contributing

## Ground rules

- Use C11-compatible portable core code; host safety gates also compile the framework as C17.
- Keep `core/` and `domain/` platform-agnostic.
- Public headers must document ownership, preconditions, postconditions, and failure modes.
- Do not introduce dynamic allocation into hot paths.
- Do not introduce hidden global state.
- Prefer simple dataflow over implicit control flow.
- Every architectural change that alters boundaries or runtime policy must get an ADR.

## Commit hygiene

- One coherent concern per commit.
- Generated files must be regenerated in the same commit as their SSOT changes.
- Build and host tests must pass before pushing. For safety-sensitive changes, also run `make host-strict-test`, `make host-sanitize-test`, and `make safety-gate`.

## Documentation

The authoritative sources live in:

- `config/*.def`
- `docs/adr/`
- public headers in `core/include/`

Do not duplicate truth manually across multiple files.
