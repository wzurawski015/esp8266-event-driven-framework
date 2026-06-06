#!/usr/bin/env python3
"""Validate long-running Wemos runtime soak transcripts without declaring fake PASS."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

SOAK_RESULT_RE = re.compile(r"EV_RUNTIME_SOAK_RESULT\s+(?P<status>PASS|FAIL)\s+(?P<body>.*)$")
METRIC_RE = re.compile(r"EV_RUNTIME_METRIC\s+(?P<body>.*)$")
KEY_VALUE_RE = re.compile(r"(?P<key>[A-Za-z0-9_]+)=(?P<value>[^\s]+)")
WIFI_UP_RE = re.compile(r"EV_NET_WIFI_UP|wifi:connected with|EV_WEMOS_SMOKE_WIFI_CONNECTED")
WDT_RESET_RE = re.compile(r"WDT reset|watchdog reset|rst cause:.*wdt", re.I)
OPERATOR_SIGINT_RE = re.compile(r"operator_sigint|CONTROLLED_MONITOR_STOP|process exited with code 130|\^C")

DEFAULT_MIN_DURATION_MIN = 30
DEFAULT_MIN_METRIC_SAMPLES = 3


def parse_kv(body: str) -> dict[str, str]:
    return {m.group("key"): m.group("value") for m in KEY_VALUE_RE.finditer(body)}


def parse_soak(text: str) -> dict[str, object]:
    metrics: list[dict[str, str]] = []
    result: dict[str, str] | None = None
    result_status = "UNKNOWN"
    for line in text.splitlines():
        metric_match = METRIC_RE.search(line)
        if metric_match is not None:
            metrics.append(parse_kv(metric_match.group("body")))
        result_match = SOAK_RESULT_RE.search(line)
        if result_match is not None:
            result_status = result_match.group("status")
            result = parse_kv(result_match.group("body"))
    return {
        "result_status": result_status,
        "result": result or {},
        "metric_samples": len(metrics),
        "wifi_seen": bool(WIFI_UP_RE.search(text)),
        "wdt_reset_seen": bool(WDT_RESET_RE.search(text)),
        "operator_sigint_seen": bool(OPERATOR_SIGINT_RE.search(text)),
    }


def _int_field(values: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(values.get(key, str(default)), 0)
    except ValueError:
        return default


def evaluate(parsed: dict[str, object], *, min_duration_min: int, min_metric_samples: int) -> list[str]:
    failures: list[str] = []
    result = parsed.get("result", {}) if isinstance(parsed.get("result"), dict) else {}
    if parsed.get("result_status") != "PASS":
        failures.append("missing EV_RUNTIME_SOAK_RESULT PASS marker")
    if _int_field(result, "duration_min") < min_duration_min:
        failures.append(f"duration_min below minimum required {min_duration_min}")
    if _int_field(result, "failures") != 0:
        failures.append("soak result reports nonzero failures")
    if str(result.get("wifi", "")).lower() != "on" and not bool(parsed.get("wifi_seen")):
        failures.append("WiFi-on evidence missing")
    if int(parsed.get("metric_samples", 0)) < min_metric_samples:
        failures.append(f"too few EV_RUNTIME_METRIC samples: {parsed.get('metric_samples', 0)}")
    if bool(parsed.get("wdt_reset_seen")):
        failures.append("watchdog reset observed")
    if bool(parsed.get("operator_sigint_seen")) and parsed.get("result_status") != "PASS":
        failures.append("operator stop observed before PASS marker")
    return failures


def self_test() -> None:
    good = """
EV_NET_WIFI_UP
EV_RUNTIME_METRIC max_actor_handler_us=900 max_mailbox_depth=3 dropped_events=0 backpressure_events=0 timer_deadline_misses=0 heap_allocations_hot_path=0
EV_RUNTIME_METRIC max_actor_handler_us=901 max_mailbox_depth=3 dropped_events=0 backpressure_events=0 timer_deadline_misses=0 heap_allocations_hot_path=0
EV_RUNTIME_METRIC max_actor_handler_us=902 max_mailbox_depth=3 dropped_events=0 backpressure_events=0 timer_deadline_misses=0 heap_allocations_hot_path=0
EV_RUNTIME_SOAK_RESULT PASS duration_min=30 wifi=on failures=0
operator_sigint
"""
    parsed = parse_soak(good)
    assert evaluate(parsed, min_duration_min=30, min_metric_samples=3) == []
    assert any("duration" in item for item in evaluate(parse_soak(good.replace("duration_min=30", "duration_min=5")), min_duration_min=30, min_metric_samples=3))
    assert any("watchdog" in item for item in evaluate(parse_soak(good + "WDT reset\n"), min_duration_min=30, min_metric_samples=3))
    assert any("PASS" in item for item in evaluate(parse_soak(good.replace("EV_RUNTIME_SOAK_RESULT PASS", "EV_RUNTIME_SOAK_RESULT FAIL")), min_duration_min=30, min_metric_samples=3))
    assert any("too few" in item for item in evaluate(parse_soak("EV_RUNTIME_SOAK_RESULT PASS duration_min=30 wifi=on failures=0\n"), min_duration_min=30, min_metric_samples=3))
    print("RUNTIME_SOAK_PARSER_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate real runtime soak evidence transcript.")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--log", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--min-duration-min", type=int, default=DEFAULT_MIN_DURATION_MIN)
    parser.add_argument("--min-metric-samples", type=int, default=DEFAULT_MIN_METRIC_SAMPLES)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.log is None:
        print("runtime-soak-gate ENVIRONMENT_BLOCKED: EV_WEMOS_RUNTIME_SOAK_LOG not set")
        return 77
    try:
        text = args.log.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        print(f"runtime-soak-gate ENVIRONMENT_BLOCKED: {exc}")
        return 77
    parsed = parse_soak(text)
    failures = evaluate(parsed, min_duration_min=args.min_duration_min, min_metric_samples=args.min_metric_samples)
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps({"parsed": parsed, "failures": failures}, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if failures:
        for failure in failures:
            print(f"RUNTIME_SOAK_FAILURE {failure}")
        print(f"runtime-soak-gate failed failures={len(failures)}")
        return 1
    print("runtime-soak-gate passed status=REAL_SOAK_PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
