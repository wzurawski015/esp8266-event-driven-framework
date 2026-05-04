#!/usr/bin/env python3
"""Serial monitor gate for the ESP8266 zero-heap I2C HIL firmware."""

import re
import sys
import time

serial = None

SUMMARY_RE = re.compile(r"HIL summary passed=(\d+) failed=(\d+) skipped=(\d+)")
STACK_RE = re.compile(r"EV_HIL_STACK task=([^ ]+) (?:high_water_words=([0-9]+)|status=not_available)")
SUCCESS_RE = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0|i2c zero-heap HIL completed successfully")
FAIL_RE = re.compile(r"EV_HIL_CASE .* FAIL|EV_HIL_RESULT FAIL|i2c zero-heap HIL failed")


def self_test() -> int:
    stack = "EV_HIL_STACK task=irq-flood high_water_words=123"
    summary = "HIL summary passed=1 failed=0 skipped=0"
    assert STACK_RE.search(stack) is not None
    assert SUMMARY_RE.search(summary) is not None
    assert FAIL_RE.search("EV_HIL_CASE x FAIL reason=y") is not None
    assert SUCCESS_RE.search("EV_HIL_RESULT PASS failures=0 skipped=0") is not None
    print("EV_HIL_MONITOR_SELF_TEST PASS")
    return 0


def main(argv: list[str]) -> int:
    if len(argv) == 2 and argv[1] == "--self-test":
        return self_test()
    if len(argv) != 4:
        print("usage: i2c_zero_heap_monitor.py <serial-port> <baud> <timeout-s>", file=sys.stderr)
        return 2

    port = argv[1]
    baud = int(argv[2])
    timeout_s = float(argv[3])
    deadline = time.monotonic() + timeout_s
    saw_success = False
    saw_summary = False

    try:
        import serial as serial_module
    except ImportError as exc:  # pragma: no cover - executed in SDK container
        raise SystemExit(f"pyserial is required: {exc}")

    with serial_module.Serial(port, baudrate=baud, timeout=0.2) as ser:
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            print(line, flush=True)

            if FAIL_RE.search(line):
                return 1

            match = SUMMARY_RE.search(line)
            if match:
                saw_summary = True
                failed = int(match.group(2))
                skipped = int(match.group(3))
                if (failed != 0) or (skipped != 0):
                    return 1

            if SUCCESS_RE.search(line):
                saw_success = True
                if saw_summary:
                    return 0

    print("error: timed out waiting for I2C zero-heap HIL success summary", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
