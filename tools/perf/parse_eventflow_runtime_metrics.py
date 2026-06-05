#!/usr/bin/env python3
"""Parse runtime event-flow budget markers from target/host transcripts."""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

DEFAULT_BUDGETS = {
    "max_actor_handler_us": 2500,
    "max_mailbox_depth": 8,
    "dropped_events": 0,
    "backpressure_events": 0,
    "timer_deadline_misses": 0,
    "heap_allocations_hot_path": 0,
}

METRIC_RE = re.compile(r"EV_RUNTIME_METRIC\s+(?P<body>.*)$")
KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z0-9_]+)=(?P<value>-?[0-9]+)")


def parse_metrics(text: str) -> dict[str, int]:
    metrics: dict[str, int] = {}
    for line in text.splitlines():
        match = METRIC_RE.search(line)
        if match is None:
            continue
        for kv in KEY_VALUE_RE.finditer(match.group("body")):
            metrics[kv.group("key")] = int(kv.group("value"))
    return metrics


def evaluate(metrics: dict[str, int], budgets: dict[str, int] | None = None, require_real_sample: bool = False) -> list[str]:
    budgets = DEFAULT_BUDGETS if budgets is None else budgets
    failures: list[str] = []
    if require_real_sample and metrics.get("max_actor_handler_us", 0) <= 0:
        failures.append("real budget gate requires max_actor_handler_us > 0")
    for key, limit in budgets.items():
        if key not in metrics:
            failures.append(f"missing metric {key}")
            continue
        value = metrics[key]
        if value > limit:
            failures.append(f"budget exceeded {key} value={value} limit={limit}")
    return failures


def self_test() -> None:
    valid = "EV_RUNTIME_METRIC max_actor_handler_us=900 max_mailbox_depth=3 dropped_events=0 backpressure_events=0 timer_deadline_misses=0 heap_allocations_hot_path=0"
    metrics = parse_metrics(valid)
    assert evaluate(metrics) == []
    invalid = valid.replace("max_actor_handler_us=900", "max_actor_handler_us=9999")
    assert any("max_actor_handler_us" in item for item in evaluate(parse_metrics(invalid)))
    assert any("missing metric" in item for item in evaluate({}))
    synthetic_zero = valid.replace("max_actor_handler_us=900", "max_actor_handler_us=0")
    assert evaluate(parse_metrics(synthetic_zero)) == []
    assert any("real budget gate" in item for item in evaluate(parse_metrics(synthetic_zero), require_real_sample=True))
    print("EVENTFLOW_RUNTIME_METRICS_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check event-driven runtime budget markers.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--require-real-sample", action="store_true", help="reject synthetic zero-duration markers in gating mode")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.log is None:
        print("eventflow-runtime-budget ENVIRONMENT_BLOCKED: no log supplied")
        return 77
    try:
        text = args.log.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        print(f"eventflow-runtime-budget ENVIRONMENT_BLOCKED: {exc}")
        return 77
    metrics = parse_metrics(text)
    failures = evaluate(metrics, require_real_sample=args.require_real_sample)
    if args.json is not None:
        args.json.write_text(json.dumps({"metrics": metrics, "failures": failures}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures:
            print(f"EVENTFLOW_RUNTIME_BUDGET_FAILURE {failure}")
        print(f"eventflow-runtime-budget failed failures={len(failures)}")
        return 1
    print("eventflow-runtime-budget passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
