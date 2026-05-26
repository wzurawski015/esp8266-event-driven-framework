#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re
import sys

REQUIRED_BENCHMARKS = {
    "static_publish_tick_fanout",
    "static_publish_with_report",
    "active_publish_tick_fanout",
    "active_publish_fault_single",
    "active_publish_with_delivery_report",
    "runtime_poll_empty",
    "runtime_poll_prefilled_mailbox",
    "runtime_loop_poll_empty",
    "runtime_loop_poll_prefilled_mailbox",
}

LINE_RE = re.compile(
    r"^BENCH\s+(?P<name>[A-Za-z0-9_]+)\s+"
    r"iterations=(?P<iterations>\d+)\s+"
    r"logical_ops=(?P<logical_ops>\d+)\s+"
    r"total_ns=(?P<total_ns>\d+)\s+"
    r"ns_per_iter=(?P<ns_per_iter>\d+)\s+"
    r"ns_per_op=(?P<ns_per_op>\d+)\s+"
    r"checksum=(?P<checksum>\d+)\s*$"
)


def parse(path: Path) -> dict[str, dict[str, int]]:
    results: dict[str, dict[str, int]] = {}
    errors: list[str] = []
    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        match = LINE_RE.match(line)
        if match is None:
            errors.append(f"{path}:{line_no}: invalid benchmark line: {line}")
            continue
        name = match.group("name")
        if name in results:
            errors.append(f"{path}:{line_no}: duplicate benchmark result: {name}")
            continue
        values = {key: int(value) for key, value in match.groupdict().items() if key != "name"}
        if values["iterations"] <= 0:
            errors.append(f"{path}:{line_no}: {name}: iterations must be positive")
        if values["logical_ops"] <= 0:
            errors.append(f"{path}:{line_no}: {name}: logical_ops must be positive")
        if values["total_ns"] <= 0:
            errors.append(f"{path}:{line_no}: {name}: total_ns must be positive")
        results[name] = values
    missing = sorted(REQUIRED_BENCHMARKS.difference(results))
    if missing:
        errors.append("missing benchmark results: " + ", ".join(missing))
    extra = sorted(set(results).difference(REQUIRED_BENCHMARKS))
    if extra:
        errors.append("unexpected benchmark results: " + ", ".join(extra))
    if errors:
        for error in errors:
            print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
    return results


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("usage: bench_report.py <bench-results.txt>", file=sys.stderr)
        return 2
    path = Path(argv[1])
    results = parse(path)
    print(f"bench-report passed: {len(results)} benchmarks")
    for name in sorted(results):
        values = results[name]
        print(
            f"bench-report {name} ns_per_iter={values['ns_per_iter']} "
            f"ns_per_op={values['ns_per_op']} logical_ops={values['logical_ops']}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
