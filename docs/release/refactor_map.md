# Refactor map

This single release artifact corresponds to these commit-equivalent phases:

1. Extracted the existing project archive and verified original host tests.
2. Moved the former demo application layer into `apps/demo`.
3. Added `runtime/` for reusable framework services.
4. Added `ev_runtime_graph_t` and `ev_runtime_builder_t`.
5. Added static actor module descriptors from `config/modules.def`.
6. Added fixed-slot timer/deadline service.
7. Added bounded ingress service.
8. Added framework-level quiescence reporting.
9. Added route QoS through `EV_ROUTE_EX`.
10. Added fault bus and fault coalescing registry from `faults.def`.
11. Added metrics registry from `metrics.def`.
12. Added actor lifecycle and capability snapshot models.
13. Added framework-level power manager.
14. Added command authentication/authorization model.
15. Added bounded network outbox backpressure.
16. Added optional trace ring.
17. Added module and driver wrapper layers.
18. Added ATNEL AIR, demo, and minimal app entry points.
19. Added routegen freshness gate.
20. Added static contract and memory budget gates.
21. Added host tests for new runtime services.
22. Added deterministic property tests.
23. Added release documentation and validation reports.
