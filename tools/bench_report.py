#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import tempfile

REQUIRED_BENCHMARKS = {
    "static_publish_tick_fanout", "static_publish_with_report", "active_publish_tick_fanout",
    "active_publish_fault_single", "active_publish_with_delivery_report", "runtime_poll_empty",
    "runtime_poll_prefilled_mailbox", "runtime_loop_poll_empty", "runtime_loop_poll_prefilled_mailbox",
}
LINE_RE = re.compile(r"^BENCH\s+(?P<name>[A-Za-z0-9_]+)\s+iterations=(?P<iterations>\d+)\s+logical_ops=(?P<logical_ops>\d+)\s+total_ns=(?P<total_ns>\d+)\s+ns_per_iter=(?P<ns_per_iter>\d+)\s+ns_per_op=(?P<ns_per_op>\d+)\s+checksum=(?P<checksum>\d+)\s*$")

def parse(path: Path) -> dict[str, dict[str, int]]:
    results: dict[str, dict[str, int]] = {}; errors: list[str] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line: continue
        m = LINE_RE.match(line)
        if m is None:
            errors.append(f"{path}:{line_no}: invalid benchmark line: {line}"); continue
        name = m.group("name")
        if name in results:
            errors.append(f"{path}:{line_no}: duplicate benchmark result: {name}"); continue
        values = {k: int(v) for k, v in m.groupdict().items() if k != "name"}
        for field in ["iterations", "logical_ops", "total_ns", "ns_per_op"]:
            if values[field] <= 0: errors.append(f"{path}:{line_no}: {name}: {field} must be positive")
        if values["checksum"] == 0 and "empty" not in name:
            errors.append(f"{path}:{line_no}: {name}: checksum must be nonzero")
        results[name] = values
    missing = sorted(REQUIRED_BENCHMARKS.difference(results))
    if missing: errors.append("missing benchmark results: " + ", ".join(missing))
    extra = sorted(set(results).difference(REQUIRED_BENCHMARKS))
    if extra: errors.append("unexpected benchmark results: " + ", ".join(extra))
    if errors:
        for e in errors: print(f"error: {e}", file=sys.stderr)
        raise SystemExit(1)
    return results

def load_budgets(path: Path) -> dict[str, dict[str, int | bool]]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if raw.get("version") != 1: raise SystemExit("error: perf budget schema version must be 1")
    benches = raw.get("benchmarks")
    if not isinstance(benches, dict): raise SystemExit("error: perf budget file missing benchmarks object")
    missing = sorted(REQUIRED_BENCHMARKS.difference(benches))
    if missing: raise SystemExit("error: perf budget file missing required benchmarks: " + ", ".join(missing))
    for name, budget in benches.items():
        if name not in REQUIRED_BENCHMARKS: raise SystemExit(f"error: perf budget file has unexpected benchmark: {name}")
        for field in ["baseline_ns_per_op", "hard_max_ns_per_op", "required"]:
            if field not in budget: raise SystemExit(f"error: perf budget {name} missing {field}")
        if int(budget["baseline_ns_per_op"]) <= 0 or int(budget["hard_max_ns_per_op"]) <= 0:
            raise SystemExit(f"error: perf budget {name} has non-positive limits")
        if int(budget["hard_max_ns_per_op"]) < int(budget["baseline_ns_per_op"]):
            raise SystemExit(f"error: perf budget {name} hard limit is below baseline")
    return benches

def report_results(results: dict[str, dict[str, int]]) -> None:
    print(f"bench-report passed: {len(results)} benchmarks")
    for name in sorted(results):
        v = results[name]
        print(f"bench-report {name} ns_per_iter={v['ns_per_iter']} ns_per_op={v['ns_per_op']} logical_ops={v['logical_ops']}")

def check_budgets(results: dict[str, dict[str, int]], budgets: dict[str, dict[str, int | bool]], report_only: bool = False) -> int:
    failures: list[str] = []; warnings: list[str] = []
    for name in sorted(REQUIRED_BENCHMARKS):
        v = results[name]; b = budgets[name]; ns = v["ns_per_op"]; hard = int(b["hard_max_ns_per_op"]); warn = int(b.get("warning_max_ns_per_op", hard))
        if ns > hard: failures.append(f"{name}: ns_per_op={ns} exceeds hard_max_ns_per_op={hard}")
        elif ns > warn: warnings.append(f"{name}: ns_per_op={ns} exceeds warning_max_ns_per_op={warn}")
    for w in warnings: print(f"bench-budget warning: {w}")
    if failures:
        for f in failures: print(f"bench-budget failure: {f}", file=sys.stderr)
        if report_only:
            print("bench-budget report-only: hard-budget failures observed but not enforced"); return 0
        return 1
    print(f"bench-budget passed: {len(results)} benchmarks within hard budgets")
    return 0

def self_test() -> int:
    sample = """BENCH static_publish_tick_fanout iterations=10 logical_ops=90 total_ns=90 ns_per_iter=9 ns_per_op=1 checksum=1\nBENCH static_publish_with_report iterations=10 logical_ops=90 total_ns=90 ns_per_iter=9 ns_per_op=1 checksum=1\nBENCH active_publish_tick_fanout iterations=10 logical_ops=90 total_ns=900 ns_per_iter=90 ns_per_op=10 checksum=1\nBENCH active_publish_fault_single iterations=10 logical_ops=10 total_ns=100 ns_per_iter=10 ns_per_op=10 checksum=1\nBENCH active_publish_with_delivery_report iterations=10 logical_ops=10 total_ns=100 ns_per_iter=10 ns_per_op=10 checksum=1\nBENCH runtime_poll_empty iterations=10 logical_ops=10 total_ns=100 ns_per_iter=10 ns_per_op=10 checksum=0\nBENCH runtime_poll_prefilled_mailbox iterations=10 logical_ops=80 total_ns=800 ns_per_iter=80 ns_per_op=10 checksum=1\nBENCH runtime_loop_poll_empty iterations=10 logical_ops=10 total_ns=100 ns_per_iter=10 ns_per_op=10 checksum=0\nBENCH runtime_loop_poll_prefilled_mailbox iterations=10 logical_ops=80 total_ns=800 ns_per_iter=80 ns_per_op=10 checksum=1\n"""
    budget = {"version": 1, "benchmarks": {name: {"baseline_ns_per_op": 10, "hard_max_ns_per_op": 20, "required": True} for name in REQUIRED_BENCHMARKS}}
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp); rp = d / "r.txt"; bp = d / "b.json"
        rp.write_text(sample, encoding="utf-8"); bp.write_text(json.dumps(budget), encoding="utf-8")
        parsed = parse(rp); assert check_budgets(parsed, load_budgets(bp)) == 0
        rp.write_text(sample + "BENCH runtime_loop_poll_prefilled_mailbox iterations=1 logical_ops=1 total_ns=1 ns_per_iter=1 ns_per_op=1 checksum=1\n", encoding="utf-8")
        try: parse(rp)
        except SystemExit: pass
        else: raise AssertionError("duplicate benchmark was not rejected")
        budget["benchmarks"]["static_publish_tick_fanout"]["hard_max_ns_per_op"] = 0; bp.write_text(json.dumps(budget), encoding="utf-8")
        try: load_budgets(bp)
        except SystemExit: pass
        else: raise AssertionError("invalid budget was not rejected")
    print("bench-report self-test passed"); return 0

def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(); ap.add_argument("results", nargs="?"); ap.add_argument("--budget", default="config/perf_budgets.json"); ap.add_argument("--report-only", action="store_true"); ap.add_argument("--strict", action="store_true"); ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args(argv[1:])
    if a.self_test: return self_test()
    if a.results is None: ap.error("results file is required unless --self-test is used")
    results = parse(Path(a.results)); report_results(results)
    if a.strict or not a.report_only: return check_budgets(results, load_budgets(Path(a.budget)), report_only=a.report_only)
    return 0
if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
