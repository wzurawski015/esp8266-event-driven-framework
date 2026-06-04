#!/usr/bin/env python3
"""Project-owned SDK warning policy.

ESP8266 RTOS SDK emits third-party warnings that are outside this repository's
control.  This gate fails only for compiler warnings in project-owned paths so
SDK builds can become warning-clean without globally suppressing vendor noise.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

PROJECT_PREFIXES = (
    "/work/adapters/esp8266_rtos_sdk/components/ev_platform/",
    "adapters/esp8266_rtos_sdk/components/ev_platform/",
    "/work/core/",
    "core/",
    "/work/runtime/",
    "runtime/",
    "/work/actors/",
    "actors/",
    "/work/drivers/",
    "drivers/",
    "/work/ports/",
    "ports/",
)

WARNING_RE = re.compile(r"^(?P<path>[^:\n]+):(?P<line>\d+):(?P<col>\d+):\s+warning:\s+(?P<message>.*)$")
NOISE_ALLOWLIST = (
    "pkg_resources is deprecated",
    "CONFIG_MONITOR_BAUD was replaced",
)


def is_project_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return any(normalized.startswith(prefix) or ("/" + normalized).startswith(prefix) for prefix in PROJECT_PREFIXES)


def project_warnings(text: str) -> list[str]:
    warnings: list[str] = []
    for raw in text.splitlines():
        if any(noise in raw for noise in NOISE_ALLOWLIST):
            continue
        match = WARNING_RE.match(raw.strip())
        if match is None:
            continue
        path = match.group("path")
        if is_project_path(path):
            warnings.append(f"{path}:{match.group('line')}:{match.group('col')}: warning: {match.group('message')}")
    return warnings


def self_test() -> None:
    sdk_noise = "/opt/esp/ESP8266_RTOS_SDK/tools/check_python_dependencies.py:22: UserWarning: pkg_resources is deprecated as an API."
    project = "/work/adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c:239:13: warning: 'f' defined but not used [-Wunused-function]"
    assert project_warnings(sdk_noise) == []
    found = project_warnings(project)
    assert len(found) == 1 and "ev_net_adapter.c" in found[0]
    assert project_warnings(project.replace("warning:", "note:")) == []
    print("SDK_PROJECT_WARNING_POLICY_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail on compiler warnings from project-owned SDK code.")
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.log is None:
        print("sdk-project-warning-policy ENVIRONMENT_BLOCKED: no SDK build log supplied")
        return 77
    try:
        text = args.log.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        print(f"sdk-project-warning-policy ENVIRONMENT_BLOCKED: {exc}")
        return 77
    warnings = project_warnings(text)
    if warnings:
        for warning in warnings:
            print(f"PROJECT_WARNING {warning}")
        print(f"sdk-project-warning-policy failed warnings={len(warnings)}")
        return 1
    print("sdk-project-warning-policy passed warnings=0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
