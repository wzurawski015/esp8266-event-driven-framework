#!/usr/bin/env python3
"""Serial monitor gate for the ESP8266 WiFi reconnect HIL firmware."""

from __future__ import annotations

import os
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial
except ImportError as exc:  # pragma: no cover - executed in SDK container
    raise SystemExit(f"pyserial is required: {exc}")

SUMMARY_RE = re.compile(r"HIL summary passed=(\d+) failed=(\d+) skipped=(\d+)")
RESULT_PASS_RE = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0")
RESULT_FAIL_RE = re.compile(r"EV_HIL_RESULT FAIL|EV_HIL_CASE .* FAIL")
WIFI_UP_RE = re.compile(r"EV_HIL_WIFI_UP\b")
WIFI_DOWN_RE = re.compile(r"EV_HIL_WIFI_DOWN\b")
NET_STATS_RE = re.compile(r"EV_HIL_NET_STATS\b(?P<body>.*)")
WDT_STATS_RE = re.compile(r"EV_HIL_WDT_STATS\b")
HEAP_RE = re.compile(r"EV_HIL_HEAP\b")
PHASE_RE = re.compile(r"EV_HIL_PHASE\s+(?P<phase>[A-Za-z0-9_-]+)")

REQUIRED_PHASES = {
    "boot-connect",
    "ap-loss",
    "recovery",
    "wdt-health-under-net-storm",
    "heap-delta-window",
}


def _parse_key_values(body: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for token in body.strip().split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        result[key] = value
    return result


def _write_release_log(lines: list[str]) -> Path:
    root = Path(os.environ.get("EV_HIL_WIFI_LOG_DIR", "/work/logs/hil/wifi"))
    root.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = root / f"wifi-reconnect-{stamp}.log"
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: wifi_reconnect_monitor.py <serial-port> <baud> <timeout-s>", file=sys.stderr)
        return 2

    port = argv[1]
    baud = int(argv[2])
    timeout_s = float(argv[3])
    deadline = time.monotonic() + timeout_s

    lines: list[str] = []
    phases_seen: set[str] = set()
    saw_wifi_up = False
    saw_wifi_down = False
    saw_recovery_sequence = False
    saw_wdt_stats = False
    saw_heap = False
    saw_summary = False
    saw_success = False
    saw_net_stats = False
    last_net_stats: dict[str, str] = {}

    with serial.Serial(port, baudrate=baud, timeout=0.2) as ser:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            print(line, flush=True)
            lines.append(line)

            if RESULT_FAIL_RE.search(line):
                _write_release_log(lines)
                return 1

            phase_match = PHASE_RE.search(line)
            if phase_match:
                phases_seen.add(phase_match.group("phase"))

            if WIFI_UP_RE.search(line):
                if saw_wifi_down:
                    saw_recovery_sequence = True
                saw_wifi_up = True

            if WIFI_DOWN_RE.search(line):
                saw_wifi_down = True

            stats_match = NET_STATS_RE.search(line)
            if stats_match:
                saw_net_stats = True
                last_net_stats = _parse_key_values(stats_match.group("body"))

            if WDT_STATS_RE.search(line):
                saw_wdt_stats = True

            if HEAP_RE.search(line):
                saw_heap = True

            summary_match = SUMMARY_RE.search(line)
            if summary_match:
                saw_summary = True
                failed = int(summary_match.group(2))
                skipped = int(summary_match.group(3))
                if failed != 0 or skipped != 0:
                    _write_release_log(lines)
                    return 1

            if RESULT_PASS_RE.search(line):
                saw_success = True
                if saw_summary:
                    break

    missing = REQUIRED_PHASES - phases_seen
    if missing:
        print(f"error: missing HIL phases: {sorted(missing)}", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_wifi_up:
        print("error: WIFI_UP was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_wifi_down:
        print("error: WIFI_DOWN was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_recovery_sequence:
        print("error: WIFI_DOWN -> WIFI_UP recovery sequence was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_net_stats:
        print("error: EV_HIL_NET_STATS was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if "reconnect_attempts" not in last_net_stats or "reconnect_suppressed" not in last_net_stats:
        print("error: reconnect_attempts/reconnect_suppressed missing from net stats", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if "high_watermark" not in last_net_stats:
        print("error: high_watermark missing from net stats", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if "pending" in last_net_stats and "high_watermark" in last_net_stats:
        try:
            high_watermark = int(last_net_stats["high_watermark"])
            if high_watermark < 0:
                raise ValueError
        except ValueError:
            print("error: invalid high_watermark value", file=sys.stderr)
            _write_release_log(lines)
            return 1
    if not saw_wdt_stats:
        print("error: EV_HIL_WDT_STATS was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_heap:
        print("error: EV_HIL_HEAP was not observed", file=sys.stderr)
        _write_release_log(lines)
        return 1
    if not saw_summary or not saw_success:
        print("error: timed out waiting for WiFi reconnect HIL PASS summary", file=sys.stderr)
        _write_release_log(lines)
        return 1

    log_path = _write_release_log(lines)
    print(f"wifi reconnect HIL log saved to {log_path}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
