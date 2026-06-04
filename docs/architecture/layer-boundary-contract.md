# Enforced layer-boundary contract

`make architecture-layer-gate` runs `tools/audit/layer_boundary_policy.py` as a
fast, conservative Clean Architecture guard.  It is not a C compiler; it checks
include paths and obvious forbidden symbol families that would collapse the
portable framework boundary.

| Layer | May depend on | Must not know directly |
|---|---|---|
| `core/` | C standard library, generated core headers, abstract ports | ESP8266 SDK, BSP, apps, adapters, concrete actor implementations |
| `runtime/` | core, generated catalogs, abstract ports, module descriptors | BSP, ESP8266 SDK, target build assumptions, adapter internals |
| `ports/` | standard C and small portable types | ESP8266 SDK, actors, apps, BSP |
| `drivers/` | pure driver contracts and abstract ports | runtime graph, mailboxes, adapters, BSP, app policy |
| `actors/` | runtime actor APIs, ports, pure drivers | adapter internals, BSP pin maps, SDK APIs |
| `adapters/` | ports and SDK/BSP-facing bootstrap contracts | concrete actor internals |
| `apps/` | composition root wiring | reusable protocol algorithms that belong in drivers |

Known legacy `*_actor_driver.h` re-export facades are explicitly allowlisted so
existing ABI remains stable.  New sensor drivers such as BH1750 and BME280 must
not use those exceptions: their protocol logic belongs in `drivers/`, while
state-machine scheduling, retry/backoff and publishing belong in `actors/`.
