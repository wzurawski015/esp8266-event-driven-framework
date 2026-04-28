# Contributing

## Ground rules

- Use C11 for portable core code.
- Keep `core/` and `domain/` platform-agnostic.
- Public headers must document ownership, preconditions, postconditions, and failure modes.
- Do not introduce dynamic allocation into hot paths.
- Do not introduce hidden global state.
- Prefer simple dataflow over implicit control flow.
- Every architectural change that alters boundaries or runtime policy must get an ADR.

## Commit hygiene

- One coherent concern per commit.
- Generated files must be regenerated in the same commit as their SSOT changes.
- Build and host tests must pass before pushing.

## Documentation

The authoritative sources live in:

- `config/*.def`
- `docs/adr/`
- public headers in `core/include/`

Do not duplicate truth manually across multiple files.
