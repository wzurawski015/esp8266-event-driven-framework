#!/usr/bin/env python3
from __future__ import annotations
import re
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
QOS_VALUES = ["EV_ROUTE_QOS_CRITICAL","EV_ROUTE_QOS_BEST_EFFORT","EV_ROUTE_QOS_LOSSY","EV_ROUTE_QOS_COALESCED","EV_ROUTE_QOS_LATEST_ONLY","EV_ROUTE_QOS_WAKEUP_CRITICAL","EV_ROUTE_QOS_TELEMETRY","EV_ROUTE_QOS_COMMAND"]
def read(rel: str) -> str:
    path = ROOT / rel
    if not path.exists():
        raise SystemExit(f"qos-contract-check: missing {rel}")
    return path.read_text(encoding="utf-8", errors="ignore")
def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//.*", "", text)
def main() -> int:
    errors=[]
    source=strip_comments(read("runtime/src/ev_qos_contract.c"))
    delivery=strip_comments(read("runtime/src/ev_delivery_service.c"))
    graph=strip_comments(read("runtime/src/ev_runtime_graph.c"))
    mailbox=strip_comments(read("core/src/ev_mailbox.c"))
    docs=read("docs/specs/route-qos.md")+read("docs/architecture/qos_delivery_contract.md")+read("docs/release/qos_end_to_end_enforcement_report.md")+read("docs/release/qos_coalesced_latest_only_promotion_report.md")
    tests=read("tests/host/test_qos_contract_table.c")+read("tests/host/test_qos_route_module_compatibility.c")+read("tests/host/test_route_qos_delivery_policy.c")+read("tests/host/test_qos_mailbox_algorithms.c")
    makefile=read("Makefile")
    for qos in QOS_VALUES:
        if qos not in source: errors.append(f"qos contract source does not cover {qos}")
        if qos not in docs: errors.append(f"qos docs do not cover {qos}")
        if qos not in tests: errors.append(f"qos tests do not cover {qos}")
    if "ev_qos_validate_route_against_module" not in graph: errors.append("runtime builder must use ev_qos_validate_route_against_module")
    if "ev_qos_failure_is_drop_allowed" not in delivery or "EV_ROUTE_QOS_BEST_EFFORT" in delivery:
        errors.append("delivery behavior must go through central QoS contract")
    header=read("runtime/include/ev/delivery_service.h")
    for field in ["rejected_routes","qos_conflict_routes","coalesced","replaced","qos_dropped","mailbox_policy_rejected"]:
        if field not in header: errors.append(f"delivery report missing {field}")
    if "qos-contracts" not in makefile or "tools/audit/qos_contract_check.py" not in makefile:
        errors.append("Makefile must register qos-contracts")
    for test_name in ["test_qos_contract_table","test_qos_route_module_compatibility","test_qos_mailbox_algorithms"]:
        if test_name not in makefile: errors.append(f"host test not registered: {test_name}")
    if "algorithm-not-yet-" "promoted" in docs:
        errors.append("docs must not describe COALESCED/LATEST_ONLY as an unpromoted algorithm")
    if "EV_QOS_FAILURE_COALESCE" not in source or "allows_coalesce" not in source:
        errors.append("COALESCED QoS must map to a promoted coalesce contract")
    if "EV_QOS_FAILURE_REPLACE_LATEST" not in source or "allows_latest_replace" not in source:
        errors.append("LATEST_ONLY QoS must map to a promoted latest-replace contract")
    if "ev_mailbox_push_qos" not in read("core/include/ev/mailbox.h"):
        errors.append("QoS-aware mailbox enqueue API missing")
    for token in ["EV_MAILBOX_DELIVERY_COALESCED", "EV_MAILBOX_DELIVERY_REPLACED", "EV_ROUTE_QOS_COALESCED", "EV_ROUTE_QOS_LATEST_ONLY"]:
        if token not in mailbox: errors.append(f"mailbox QoS algorithm missing {token}")
    if "ev_actor_registry_delivery_qos" not in delivery:
        errors.append("delivery service must call ev_actor_registry_delivery_qos")
    if "ev_mailbox_push_qos" not in mailbox:
        errors.append("mailbox core must implement ev_mailbox_push_qos")
    if errors:
        for e in errors: print("qos-contract-check: "+e)
        return 1
    print("qos contract check passed")
    return 0
if __name__ == "__main__":
    raise SystemExit(main())
