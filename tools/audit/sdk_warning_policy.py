#!/usr/bin/env python3
"""Project-owned SDK warning policy."""
from __future__ import annotations

import argparse
import re
from pathlib import Path

PROJECT_PREFIXES = ("/work/adapters/esp8266_rtos_sdk/components/ev_platform/", "adapters/esp8266_rtos_sdk/components/ev_platform/", "/work/core/", "core/", "/work/runtime/", "runtime/", "/work/actors/", "actors/", "/work/drivers/", "drivers/", "/work/ports/", "ports/")
WARNING_RE = re.compile(r"^(?P<path>[^:\n]+):(?P<line>\d+):(?P<col>\d+):\s+warning:\s+(?P<message>.*)$")
SDK_TARGET_SESSION_RE = re.compile(r"make: Entering directory '[^']*/adapters/esp8266_rtos_sdk/targets/[^']+'")
NOISE_ALLOWLIST = ("pkg_resources is deprecated", "CONFIG_MONITOR_BAUD was replaced")


def is_project_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return any(normalized.startswith(prefix) or ("/" + normalized).startswith(prefix) for prefix in PROJECT_PREFIXES)


def latest_build_session(text: str) -> str:
    matches = list(SDK_TARGET_SESSION_RE.finditer(text))
    if not matches:
        return text
    return text[matches[-1].start():]


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
    sdk_config = "/work/adapters/esp8266_rtos_sdk/targets/wemos/sdkconfig:18 CONFIG_MONITOR_BAUD was replaced with CONFIG_ESPTOOLPY_MONITOR_BAUD"
    project = "/work/adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c:239:13: warning: 'f' defined but not used [-Wunused-function]"
    assert project_warnings(sdk_noise) == []
    assert project_warnings(sdk_config) == []
    assert len(project_warnings(project)) == 1
    assert project_warnings(project.replace("warning:", "note:")) == []
    transcript = "\n".join(["make: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'", project, "make: Leaving directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'", "make: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/atnel_air_esp_motherboard'", sdk_noise])
    assert project_warnings(transcript) != []
    assert project_warnings(latest_build_session(transcript)) == []
    latest_bad = transcript + "\nmake: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'\n" + project
    assert project_warnings(latest_build_session(latest_bad)) != []
    print("SDK_PROJECT_WARNING_POLICY_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail on compiler warnings from project-owned SDK code.")
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--project-only", action="store_true", help="accepted for explicit CI readability; project-only is the default policy")
    parser.add_argument("--latest-build-session", action="store_true", help="check only the last SDK target build session in a multi-session transcript")
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
    if args.latest_build_session:
        text = latest_build_session(text)
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
