#!/usr/bin/env python3
"""Serial monitor gate for the ESP8266 zero-heap I2C HIL firmware."""

import re
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover - executed in SDK container
    raise SystemExit(f"pyserial is required: {exc}")

SUMMARY_RE = re.compile(r"HIL summary passed=(\d+) failed=(\d+) skipped=(\d+)")
SUCCESS_RE = re.compile(r"EV_HIL_RESULT PASS failures=0 skipped=0|i2c zero-heap HIL completed successfully")
FAIL_RE = re.compile(r"EV_HIL_CASE .* FAIL|EV_HIL_RESULT FAIL|i2c zero-heap HIL failed")


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("usage: i2c_zero_heap_monitor.py <serial-port> <baud> <timeout-s>", file=sys.stderr)
        return 2

    port = argv[1]
    baud = int(argv[2])
    timeout_s = float(argv[3])
    deadline = time.monotonic() + timeout_s
    saw_success = False
    saw_summary = False

    with serial.Serial(port, baudrate=baud, timeout=0.2) as ser:
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
