# Zero UB static-analysis/coverage/fuzz report

Status: infrastructure added.

New gates:

- `make static-analysis-gate`
- `make coverage-report`
- `make coverage-gate`
- `make fuzz-smoke-gate`
- `make fuzz-sanitize-gate`
- `make ub-hardening-gate`

The gates are intentionally honest: missing analyzer/coverage/fuzzer tooling produces
`ENVIRONMENT_BLOCKED`, not fake PASS. Deterministic fuzz smoke is always expected to run
with the normal host toolchain.

Impact on Filar 4: raises Zero-UB readiness by adding static-analysis, coverage and fuzz
contract layers on top of existing C17/Werror and sanitizer gates.
