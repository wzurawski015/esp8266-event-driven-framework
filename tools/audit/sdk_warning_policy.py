#!/usr/bin/env python3
"""Project-owned SDK warning policy.

This gate intentionally distinguishes repository-owned compiler diagnostics from
vendor SDK noise.  In Phase 6 the canonical build-session boundary is an
EV_SDK_BUILD_BEGIN..EV_SDK_BUILD_END block emitted by tools/fw sdk-build-one;
the older "latest make directory" heuristic is only a legacy fallback.
"""
from __future__ import annotations

import argparse
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

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
SDK_TARGET_SESSION_RE = re.compile(r"make: Entering directory '[^']*/adapters/esp8266_rtos_sdk/targets/[^']+'")
SDK_BEGIN_RE = re.compile(r"^EV_SDK_BUILD_BEGIN\s*$", re.M)
SDK_END_RE = re.compile(r"^EV_SDK_BUILD_END\s*$", re.M)
SDK_TARGET_RE = re.compile(r"^EV_SDK_BUILD_TARGET=(?P<target>\S+)\s*$", re.M)
SDK_STATUS_RE = re.compile(r"^EV_SDK_BUILD_STATUS=(?P<status>PASS|FAIL)\s*$", re.M)
NOISE_ALLOWLIST = ("pkg_resources is deprecated", "CONFIG_MONITOR_BAUD was replaced")


@dataclass(frozen=True)
class WarningFinding:
    path: str
    line: int
    column: int
    message: str

    def format(self) -> str:
        return f"{self.path}:{self.line}:{self.column}: warning: {self.message}"


@dataclass(frozen=True)
class SessionSelection:
    status: str
    text: str
    mode: str
    reason: str = ""
    target: str = ""


def is_project_path(path: str) -> bool:
    normalized = path.replace("\\", "/")
    return any(normalized.startswith(prefix) or ("/" + normalized).startswith(prefix) for prefix in PROJECT_PREFIXES)


def _target_for_block(block: str) -> str:
    match = SDK_TARGET_RE.search(block)
    return match.group("target") if match else ""


def extract_build_blocks(text: str) -> list[str]:
    """Return complete EV_SDK_BUILD_BEGIN..END blocks, including target preamble."""
    lines = text.splitlines(keepends=True)
    blocks: list[str] = []
    pending_prefix: list[str] = []
    current: list[str] | None = None

    for raw in lines:
        if raw.startswith("EV_SDK_BUILD_TARGET=") or raw.startswith("EV_SDK_BUILD_PROJECT=") or raw.startswith("EV_SDK_BUILD_VARIANT="):
            if current is None:
                pending_prefix.append(raw)
            else:
                current.append(raw)
            continue
        if SDK_BEGIN_RE.match(raw.rstrip("\n")):
            current = pending_prefix + [raw]
            pending_prefix = []
            continue
        if current is not None:
            current.append(raw)
            if SDK_END_RE.match(raw.rstrip("\n")):
                blocks.append("".join(current))
                current = None
            continue
        if raw.strip() and not raw.startswith("EV_SDK_BUILD_"):
            pending_prefix = []

    return blocks


def select_build_session(text: str,
                         latest: bool = False,
                         target: str | None = None,
                         strict: bool = False) -> SessionSelection:
    blocks = extract_build_blocks(text)
    if target:
        blocks = [block for block in blocks if _target_for_block(block) == target]
    if blocks:
        selected = blocks[-1] if latest else "\n".join(blocks)
        return SessionSelection(status="PASS", text=selected, mode="EV_SDK_BUILD_BLOCK", target=_target_for_block(selected))

    if strict:
        return SessionSelection(status="ENVIRONMENT_BLOCKED", text="", mode="STRICT_BUILD_SESSION", reason="no complete EV_SDK_BUILD_BEGIN/END block found")

    if latest:
        matches = list(SDK_TARGET_SESSION_RE.finditer(text))
        if matches:
            return SessionSelection(status="PASS", text=text[matches[-1].start():], mode="LEGACY_SESSION_FALLBACK", reason="no EV_SDK_BUILD block; used latest target make directory")
    return SessionSelection(status="PASS", text=text, mode="FULL_LOG", reason="no session filtering applied")


def project_warning_findings(text: str) -> list[WarningFinding]:
    warnings: list[WarningFinding] = []
    for raw in text.splitlines():
        if any(noise in raw for noise in NOISE_ALLOWLIST):
            continue
        match = WARNING_RE.match(raw.strip())
        if match is None:
            continue
        path = match.group("path")
        if is_project_path(path):
            warnings.append(
                WarningFinding(
                    path=path,
                    line=int(match.group("line")),
                    column=int(match.group("col")),
                    message=match.group("message"),
                )
            )
    return warnings


def project_warnings(text: str) -> list[str]:
    return [finding.format() for finding in project_warning_findings(text)]


def latest_build_session(text: str) -> str:
    """Compatibility wrapper used by older tests/tools."""
    return select_build_session(text, latest=True, strict=False).text


def evaluate_log(text: str,
                 latest: bool = False,
                 target: str | None = None,
                 strict: bool = False) -> dict[str, object]:
    selected = select_build_session(text, latest=latest, target=target, strict=strict)
    if selected.status != "PASS":
        return {
            "status": selected.status,
            "session_mode": selected.mode,
            "reason": selected.reason,
            "warnings": [],
            "warning_count": 0,
            "target": target or "",
        }
    warnings = project_warnings(selected.text)
    return {
        "status": "PASS" if not warnings else "FAIL",
        "session_mode": selected.mode,
        "reason": selected.reason,
        "warnings": warnings,
        "warning_count": len(warnings),
        "target": target or selected.target,
    }


def _assert_status(result: dict[str, object], status: str) -> None:
    assert result["status"] == status, result


def self_test() -> None:
    sdk_noise = "/opt/esp/ESP8266_RTOS_SDK/tools/check_python_dependencies.py:22: UserWarning: pkg_resources is deprecated as an API."
    sdk_config = "/work/adapters/esp8266_rtos_sdk/targets/wemos/sdkconfig:18 CONFIG_MONITOR_BAUD was replaced with CONFIG_ESPTOOLPY_MONITOR_BAUD"
    project = "/work/adapters/esp8266_rtos_sdk/components/ev_platform/ev_net_adapter.c:239:13: warning: 'f' defined but not used [-Wunused-function]"
    assert project_warnings(sdk_noise) == []
    assert project_warnings(sdk_config) == []
    assert len(project_warnings(project)) == 1
    assert project_warnings(project.replace("warning:", "note:")) == []

    older_bad = "\n".join([
        "EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650",
        "EV_SDK_BUILD_BEGIN",
        project,
        "EV_SDK_BUILD_STATUS=PASS",
        "EV_SDK_BUILD_RC=0",
        "EV_SDK_BUILD_END",
    ])
    latest_good = "\n".join([
        "EV_SDK_BUILD_TARGET=atnel_air_esp_motherboard",
        "EV_SDK_BUILD_BEGIN",
        sdk_noise,
        "EV_SDK_BUILD_STATUS=PASS",
        "EV_SDK_BUILD_RC=0",
        "EV_SDK_BUILD_END",
        "monitor: EV_WEMOS_SMOKE_TICK seq=1",
    ])
    _assert_status(evaluate_log(older_bad + "\n" + latest_good, latest=True, strict=True), "PASS")
    _assert_status(evaluate_log(latest_good + "\n" + older_bad, latest=True, strict=True), "FAIL")
    _assert_status(evaluate_log(older_bad.replace("EV_SDK_BUILD_END", ""), latest=True, strict=True), "ENVIRONMENT_BLOCKED")
    _assert_status(evaluate_log(older_bad + "\n" + latest_good, latest=True, target="wemos_esp_wroom_02_18650", strict=True), "FAIL")
    legacy = "make: Entering directory '/work/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'\n" + project
    _assert_status(evaluate_log(legacy, latest=True, strict=False), "FAIL")
    _assert_status(evaluate_log(legacy, latest=True, strict=True), "ENVIRONMENT_BLOCKED")
    print("SDK_PROJECT_WARNING_POLICY_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Fail on compiler warnings from project-owned SDK code.")
    parser.add_argument("log", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--project-only", action="store_true", help="accepted for explicit CI readability; project-only is the default policy")
    parser.add_argument("--latest-build-session", action="store_true", help="check only the latest EV_SDK_BUILD_BEGIN/END block")
    parser.add_argument("--session-kind", choices=["build"], default="build")
    parser.add_argument("--target", help="filter to a specific EV_SDK_BUILD_TARGET")
    parser.add_argument("--strict-build-session", action="store_true", help="require complete EV_SDK_BUILD_BEGIN/END markers")
    parser.add_argument("--json", type=Path, help="optional machine-readable result output")
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

    result = evaluate_log(
        text,
        latest=args.latest_build_session,
        target=args.target,
        strict=args.strict_build_session,
    )
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if result["status"] == "ENVIRONMENT_BLOCKED":
        print(f"sdk-project-warning-policy ENVIRONMENT_BLOCKED: {result['reason']}")
        return 77
    warnings = list(result.get("warnings", []))
    if warnings:
        for warning in warnings:
            print(f"PROJECT_WARNING {warning}")
        print(
            f"sdk-project-warning-policy failed warnings={len(warnings)} "
            f"session_mode={result['session_mode']} target={result.get('target', '')}"
        )
        return 1
    print(
        f"sdk-project-warning-policy passed warnings=0 "
        f"session_mode={result['session_mode']} target={result.get('target', '')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
