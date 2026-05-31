# Coverage gate report

Minimum critical-file line coverage: 20.0%

| File | Lines | Coverage | Status |
|---|---:|---:|---:|
| `core/src/ev_msg.c` | 207 | 72.5% | PASS |
| `core/src/ev_mailbox.c` | 192 | 75.5% | PASS |
| `core/src/ev_lease_pool.c` | 140 | 80.0% | PASS |
| `runtime/src/ev_power_state_machine.c` | 110 | 50.9% | PASS |
| `runtime/src/ev_qos_contract.c` | 93 | 45.2% | PASS |

Exceptions:
- `runtime/src/ev_delivery_service.c`: covered by delivery host tests and sanitizer gates; gcov attribution is unstable in the initial host coverage subset

Coverage is host-only; SDK/HIL coverage remains outside this gate.
