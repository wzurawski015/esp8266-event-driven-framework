#!/usr/bin/env python3
"""Simple raw serial monitor for Docker-first ESP8266 workflows.

This helper intentionally avoids the SDK's interactive monitor stack.
It streams raw bytes to stdout and exits cleanly on SIGINT/SIGTERM without an
extra shell layer between Docker and Python.
"""

from __future__ import annotations

import os
import signal
import sys
from typing import Any

import serial

_STOP = False
_SERIAL = None


def _request_stop(_signum: int, _frame: Any) -> None:
    global _STOP
    _STOP = True
    ser = _SERIAL
    if ser is not None:
        cancel_read = getattr(ser, "cancel_read", None)
        if callable(cancel_read):
            try:
                cancel_read()
            except Exception:
                pass


def _open_serial(port: str, baud: int) -> serial.Serial:
    kwargs: dict[str, Any] = {
        "port": port,
        "baudrate": baud,
        "timeout": 0.05,
        "xonxoff": False,
        "rtscts": False,
        "dsrdtr": False,
    }

    try:
        kwargs["exclusive"] = True
        return serial.Serial(**kwargs)
    except TypeError:
        kwargs.pop("exclusive", None)
        return serial.Serial(**kwargs)


def main() -> int:
    global _SERIAL
    if len(sys.argv) != 3:
        print("usage: serial_monitor.py <port> <baud>", file=sys.stderr)
        return 2

    port = sys.argv[1]
    baud = int(sys.argv[2])

    signal.signal(signal.SIGINT, _request_stop)
    signal.signal(signal.SIGTERM, _request_stop)

    try:
        ser = _open_serial(port, baud)
    except Exception as exc:  # pragma: no cover - direct operator feedback
        print(f"error: could not open serial port {port}: {exc}", file=sys.stderr)
        return 1

    _SERIAL = ser

    try:
        with ser:
            while not _STOP:
                chunk = ser.read(max(1, ser.in_waiting))
                if chunk:
                    os.write(sys.stdout.fileno(), chunk)
    except KeyboardInterrupt:
        pass
    finally:
        _SERIAL = None
        os.write(sys.stderr.fileno(), b"\n--- exit ---\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
