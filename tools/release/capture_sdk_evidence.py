#!/usr/bin/env python3
"""Capture ESP8266 SDK build/map/stack evidence without fabricating PASS rows.

The tool has two intentional modes:
- default capture: creates per-target evidence JSON and reports. If the SDK
  toolchain/Docker build runner is unavailable, required rows become
  ENVIRONMENT_BLOCKED, not PASS.
- gate: requires required SDK targets to have real PASS evidence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_redaction import redact_text
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
SDK_BUILD_REPORT = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
SDK_MEMORY_REPORT = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"
SDK_STACK_REPORT = ROOT / "docs" / "release" / "sdk_stack_map_release_gates_report.md"
SDK_REAL_REPORT = ROOT / "docs" / "release" / "sdk_real_build_map_stack_evidence_report.md"
SDK_CANONICAL_REPORT = ROOT / "docs" / "release" / "sdk_canonical_build_evidence_capture_report.md"
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")
MEM_RE = re.compile(r"\bEV_MEM_(IRAM|DRAM|BSS|DATA|IROM|APP_BIN)\b\s*(?:used|size|=)?\s*=?\s*([0-9]+)")
STACK_RE = re.compile(r"EV_MEM_STACK_USAGE\s+status=([^\s]+).*?max_frame=([0-9]+)")
APP_BIN_RE = re.compile(r"EV_(?:SDK_)?APP_BIN_BYTES=([0-9]+)|EV_MEM_APP_BIN\s*(?:used|size|=)?\s*=?\s*([0-9]+)")
SDK_TARGET_RE = re.compile(r"\bEV_SDK_BUILD_TARGET=([A-Za-z0-9_./:-]+)\b")
SDK_STATUS_PASS_RE = re.compile(r"\bEV_SDK_BUILD_STATUS=PASS\b")
SDK_STATUS_FAIL_RE = re.compile(r"\bEV_SDK_BUILD_STATUS=FAIL\b")
SDK_RC_RE = re.compile(r"\bEV_SDK_BUILD_RC=([0-9]+)\b")
SDK_BEGIN_RE = re.compile(r"\bEV_SDK_BUILD_BEGIN\b")
SDK_END_RE = re.compile(r"\bEV_SDK_BUILD_END\b")
MEM_REPORT_PASS_RE = re.compile(r"\bEV_MEM_REPORT_RESULT\s+PASS\b")
SECRET_PATTERNS = [
    re.compile(r'(EV_BOARD_NET_WIFI_PASSWORD\s+)([^\s]+)'),
    re.compile(r'(EV_BOARD_NET_WIFI_SSID\s+)([^\s]+)'),
    re.compile(r'(EV_BOARD_NET_COMMAND_TOKEN\s+)([^\s]+)'),
    re.compile(r'(WIFI_PASSWORD[=:\s]+)([^\s]+)'),
    re.compile(r'(COMMAND_TOKEN[=:\s]+)([^\s]+)'),
]
SELF_TEST_MARKERS = ["target=self-test", "--self-test", "SELF_TEST PASS", "EV_MEM_REPORT_RESULT PASS target=self-test"]
MIXED_TRANSCRIPT_MARKERS = ["EV_HIL_", "EV_WEMOS_SMOKE_", "EV_POWER_SMOKE_", "esptool.py", "Hash of data verified.", "make quality-gate", "make host-test"]
REQUIRED_CLASSES = {"buildable_sdk", "physical_smoke", "hil_sdk"}

@dataclass(frozen=True)
class Target:
    name: str
    path: str
    klass: str
    baud: str
    family: str


def parse_targets() -> list[Target]:
    targets: list[Target] = []
    for raw in TARGETS_DEF.read_text(encoding="utf-8").splitlines():
        m = TARGET_RE.match(raw)
        if m:
            targets.append(Target(*(part.strip() for part in m.groups())))
    if not targets:
        raise RuntimeError(f"no SDK targets parsed from {TARGETS_DEF}")
    return targets


def redact(text: str) -> str:
    return redact_text(text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def toolchain_status() -> tuple[bool, str]:
    if os.environ.get("EV_SDK_EVIDENCE_FORCE_BLOCKED") == "1":
        return False, "EV_SDK_EVIDENCE_FORCE_BLOCKED=1"
    if os.environ.get("IDF_PATH") and (ROOT / "tools" / "fw").is_file():
        return True, "IDF_PATH present"
    if shutil.which(os.environ.get("DOCKER_BIN", "docker")) and (ROOT / "tools" / "fw").is_file():
        return True, "docker-based SDK runner available"
    return False, "SDK build toolchain not available: neither IDF_PATH nor docker runner found"


def parse_memory(text: str) -> dict[str, int]:
    values = {"IRAM": 0, "DRAM": 0, "BSS": 0, "DATA": 0, "IROM": 0, "APP_BIN": 0}
    for m in MEM_RE.finditer(text):
        values[m.group(1)] = int(m.group(2), 10)
    for m in APP_BIN_RE.finditer(text):
        values["APP_BIN"] = int(m.group(1) or m.group(2), 10)
    return values


def parse_stack(text: str) -> dict[str, object]:
    m = STACK_RE.search(text)
    if not m:
        return {"status": "STACK_NOT_AVAILABLE", "max_frame": 0, "reason": "no EV_MEM_STACK_USAGE marker"}
    status = m.group(1)
    max_frame = int(m.group(2), 10)
    if status in {"ok", "unchecked"}:
        return {"status": "PASS", "max_frame": max_frame, "reason": status}
    return {"status": "STACK_NOT_AVAILABLE", "max_frame": max_frame, "reason": status}




def marker_summary(target: Target, text: str, command_rc: int | None = None) -> tuple[str, str, dict[str, object]]:
    targets_seen = SDK_TARGET_RE.findall(text)
    distinct_targets = sorted(set(targets_seen))
    values = parse_memory(text)
    build_rc_values = [int(m.group(1), 10) for m in SDK_RC_RE.finditer(text)]
    build_rc = build_rc_values[-1] if build_rc_values else command_rc
    target_marker_seen = bool(targets_seen)
    target_marker_match = target_marker_seen and len(distinct_targets) == 1 and distinct_targets[0] == target.name
    build_status_marker_seen = bool(SDK_STATUS_PASS_RE.search(text))
    build_fail_marker_seen = bool(SDK_STATUS_FAIL_RE.search(text))
    build_begin_seen = bool(SDK_BEGIN_RE.search(text))
    build_end_seen = bool(SDK_END_RE.search(text))
    self_test_marker_seen = any(marker in text for marker in SELF_TEST_MARKERS)
    mixed_transcript_detected = self_test_marker_seen or any(marker in text for marker in MIXED_TRANSCRIPT_MARKERS)
    app_bin_nonzero = int(values.get("APP_BIN", 0) or 0) > 0
    memory_nonzero = any(int(values.get(k, 0) or 0) > 0 for k in ["IRAM", "DRAM", "BSS", "DATA", "IROM"])
    mem_report_pass_seen = bool(MEM_REPORT_PASS_RE.search(text))
    checks = {
        "evidence_kind": "sdk_build",
        "target_marker_seen": target_marker_seen,
        "target_markers": targets_seen,
        "target_marker_match": target_marker_match,
        "build_begin_seen": build_begin_seen,
        "build_end_seen": build_end_seen,
        "build_status_marker_seen": build_status_marker_seen,
        "build_fail_marker_seen": build_fail_marker_seen,
        "build_rc_marker_seen": build_rc is not None,
        "build_rc": build_rc,
        "mem_report_pass_seen": mem_report_pass_seen,
        "self_test_marker_seen": self_test_marker_seen,
        "mixed_transcript_detected": mixed_transcript_detected,
        "app_bin_nonzero": app_bin_nonzero,
        "memory_nonzero": memory_nonzero,
    }
    failures: list[str] = []
    if not target_marker_seen:
        failures.append("missing EV_SDK_BUILD_TARGET marker")
    elif not target_marker_match:
        failures.append(f"EV_SDK_BUILD_TARGET mismatch: expected {target.name}, saw {','.join(distinct_targets)}")
    if not build_begin_seen:
        failures.append("missing EV_SDK_BUILD_BEGIN")
    if not build_end_seen:
        failures.append("missing EV_SDK_BUILD_END")
    if not build_status_marker_seen:
        failures.append("missing EV_SDK_BUILD_STATUS=PASS")
    if build_fail_marker_seen:
        failures.append("EV_SDK_BUILD_STATUS=FAIL marker found")
    if build_rc is None:
        failures.append("missing EV_SDK_BUILD_RC marker")
    elif int(build_rc) != 0:
        failures.append(f"EV_SDK_BUILD_RC is non-zero: {build_rc}")
    if self_test_marker_seen:
        failures.append("self-test marker found in SDK capture evidence")
    if mixed_transcript_detected:
        failures.append("mixed transcript detected")
    if target.klass in REQUIRED_CLASSES and not app_bin_nonzero:
        failures.append("APP_BIN is zero or missing for required buildable/HIL/physical target")
    if target.klass in REQUIRED_CLASSES and not memory_nonzero:
        failures.append("all non-APP memory markers are zero or missing")
    if mem_report_pass_seen and not build_status_marker_seen:
        failures.append("EV_MEM_REPORT_RESULT PASS cannot substitute for SDK build status")
    status = "PASS" if not failures else "FAIL"
    reason = "canonical target-specific SDK build evidence" if status == "PASS" else "; ".join(failures)
    return status, reason, checks


def write_sha_manifest(directory: Path, files: Iterable[Path]) -> None:
    lines = []
    for path in files:
        if path.exists() and path.is_file():
            lines.append(f"{sha256(path)}  {path.name}")
    (directory / "sha256sums.txt").write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")


def run_build(target: Target, out_dir: Path) -> tuple[str, str, int | None]:
    """Run a real build when explicitly requested and possible.

    A default run is intentionally conservative. It records ENVIRONMENT_BLOCKED
    instead of attempting a hidden host-specific SDK build.
    """
    can_build, reason = toolchain_status()
    if not can_build:
        return "ENVIRONMENT_BLOCKED", reason, None
    if os.environ.get("EV_SDK_EVIDENCE_RUN_BUILDS") != "1":
        return "ENVIRONMENT_BLOCKED", "EV_SDK_EVIDENCE_RUN_BUILDS is not set; no real SDK build attempted", None

    cmd = ["./tools/fw", "sdk-build-one", target.name]
    log_path = out_dir / "build.log"
    proc = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = redact(proc.stdout)
    log_path.write_text(text, encoding="utf-8")
    if proc.returncode != 0:
        return "FAIL", f"SDK build command failed rc={proc.returncode}", proc.returncode
    return "PASS", "real SDK build completed", proc.returncode


def collect_target(target: Target) -> dict[str, object]:
    out_dir = EVIDENCE_ROOT / target.name
    out_dir.mkdir(parents=True, exist_ok=True)
    build_log = out_dir / "build.log"
    size_log = out_dir / "size.log"
    map_summary = out_dir / "map_summary.txt"
    stack_usage = out_dir / "stack_usage.txt"
    sdkconfig_effective = out_dir / "sdkconfig.effective.txt"

    status = "NOT_APPLICABLE" if target.klass == "metadata_only" else "ENVIRONMENT_BLOCKED"
    reason = "metadata-only target" if target.klass == "metadata_only" else "not run"
    values = {"IRAM": 0, "DRAM": 0, "BSS": 0, "DATA": 0, "IROM": 0, "APP_BIN": 0}
    stack = {"status": "STACK_NOT_AVAILABLE", "max_frame": 0, "reason": "not run"}
    marker_checks = {"evidence_kind": "sdk_build", "target_marker_seen": False, "target_marker_match": False, "build_begin_seen": False, "build_end_seen": False, "build_status_marker_seen": False, "build_rc_marker_seen": False, "build_rc": None, "self_test_marker_seen": False, "mixed_transcript_detected": False, "app_bin_nonzero": False, "memory_nonzero": False}

    if target.klass != "metadata_only":
        status, reason, command_rc = run_build(target, out_dir)
        if build_log.exists():
            text = build_log.read_text(encoding="utf-8", errors="ignore")
            values = parse_memory(text)
            stack = parse_stack(text)
            # Preserve a compact summary even when the full map is not committed.
            size_log.write_text("\n".join(f"EV_MEM_{k}={v}" for k, v in values.items()) + "\n", encoding="utf-8")
            map_summary.write_text(
                f"target={target.name}\nstatus={status}\n"
                + "\n".join(f"EV_MEM_{k}={v}" for k, v in values.items()) + "\n",
                encoding="utf-8",
            )
            stack_usage.write_text(json.dumps(stack, indent=2, sort_keys=True) + "\n", encoding="utf-8")
            marker_status, marker_reason, marker_checks = marker_summary(target, text, command_rc)
            if status == "PASS":
                status = marker_status
                reason = marker_reason
            elif status == "FAIL" and marker_reason:
                reason = f"{reason}; {marker_reason}"
        else:
            build_log.write_text(
                f"EV_SDK_BUILD_TARGET={target.name}\n"
                f"EV_SDK_BUILD_STATUS={status}\n"
                f"EV_SDK_BUILD_REASON={reason}\n",
                encoding="utf-8",
            )
            size_log.write_text("", encoding="utf-8")
            map_summary.write_text(f"target={target.name}\nstatus={status}\nreason={reason}\n", encoding="utf-8")
            stack_usage.write_text(json.dumps(stack, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    sdkconfig = ROOT / target.path / "sdkconfig"
    defaults = ROOT / target.path / "sdkconfig.defaults"
    if sdkconfig.is_file():
        sdkconfig_effective.write_text(redact(sdkconfig.read_text(encoding="utf-8", errors="ignore")), encoding="utf-8")
    elif defaults.is_file():
        sdkconfig_effective.write_text(redact(defaults.read_text(encoding="utf-8", errors="ignore")), encoding="utf-8")
    else:
        sdkconfig_effective.write_text("sdkconfig not available\n", encoding="utf-8")

    evidence = {
        "target": target.name,
        "path": target.path,
        "class": target.klass,
        "baud": target.baud,
        "family": target.family,
        "status": status,
        "reason": reason,
        "timestamp_utc": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "build_log": str(build_log.relative_to(ROOT)),
        "size_log": str(size_log.relative_to(ROOT)),
        "map_summary": str(map_summary.relative_to(ROOT)),
        "stack_usage": str(stack_usage.relative_to(ROOT)),
        "sdkconfig_effective": str(sdkconfig_effective.relative_to(ROOT)),
        "values": values,
        "stack": stack,
        **marker_checks,
    }
    evidence_path = out_dir / "evidence.json"
    evidence_path.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_sha_manifest(out_dir, [build_log, size_log, map_summary, stack_usage, sdkconfig_effective, evidence_path])
    return evidence


def load_evidence() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for target in parse_targets():
        path = EVIDENCE_ROOT / target.name / "evidence.json"
        if path.is_file():
            rows.append(json.loads(path.read_text(encoding="utf-8")))
        else:
            rows.append({
                "target": target.name,
                "path": target.path,
                "class": target.klass,
                "status": "NOT_APPLICABLE" if target.klass == "metadata_only" else "NOT_RUN",
                "reason": "missing evidence.json",
                "values": {},
                "stack": {"status": "STACK_NOT_AVAILABLE", "max_frame": 0},
                "build_log": "",
                "evidence_kind": "sdk_build",
                "target_marker_seen": False,
                "target_marker_match": False,
                "build_status_marker_seen": False,
                "build_rc": None,
                "self_test_marker_seen": False,
                "mixed_transcript_detected": False,
            })
    return rows


def render_reports(rows: list[dict[str, object]]) -> None:
    SDK_BUILD_REPORT.write_text(render_build_report(rows), encoding="utf-8")
    SDK_MEMORY_REPORT.write_text(render_memory_report(rows), encoding="utf-8")
    SDK_STACK_REPORT.write_text(render_stack_report(rows), encoding="utf-8")
    SDK_REAL_REPORT.write_text(render_real_report(rows), encoding="utf-8")
    SDK_CANONICAL_REPORT.write_text(render_canonical_report(rows), encoding="utf-8")


def render_build_report(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK build matrix report", "", "| Target | Class | Status | Log | Reason |", "|---|---:|---:|---|---|"]
    for row in rows:
        log = row.get("build_log", "")
        log_cell = f"`{log}`" if log else ""
        lines.append(f"| `{row['target']}` | `{row['class']}` | {row['status']} | {log_cell} | {row.get('reason','')} |")
    lines += ["", "PASS rows require a committed per-target `evidence.json` and a real build log with SDK markers.", ""]
    return "\n".join(lines)


def render_memory_report(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK memory matrix report", "", "| Target | Class | Status | IRAM | DRAM | BSS | DATA | APP_BIN | Stack | Reason |", "|---|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    for row in rows:
        values = row.get("values", {}) or {}
        stack = row.get("stack", {}) or {}
        status = row.get("status", "NOT_RUN")
        stack_cell = f"{stack.get('status','STACK_NOT_AVAILABLE')}:{stack.get('max_frame',0)}"
        lines.append(
            f"| `{row['target']}` | `{row['class']}` | {status} | {values.get('IRAM',0)} | {values.get('DRAM',0)} | {values.get('BSS',0)} | {values.get('DATA',0)} | {values.get('APP_BIN',0)} | {stack_cell} | {row.get('reason','')} |"
        )
    lines += ["", "Strict mode: FAIL unless required rows have real EV_MEM markers and budgets satisfied.", ""]
    return "\n".join(lines)


def render_stack_report(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK stack/map release gates report", "", "| Target | Class | Status | Stack status | Max frame | Map summary |", "|---|---:|---:|---|---:|---|"]
    for row in rows:
        stack = row.get("stack", {}) or {}
        map_path = row.get("map_summary", "")
        lines.append(f"| `{row['target']}` | `{row['class']}` | {row['status']} | {stack.get('status','STACK_NOT_AVAILABLE')} | {stack.get('max_frame',0)} | `{map_path}` |")
    lines += ["", "A stack PASS requires `.su`-derived or parser-derived evidence. `STACK_NOT_AVAILABLE` is not PASS.", ""]
    return "\n".join(lines)


def render_real_report(rows: list[dict[str, object]]) -> str:
    summary = status_summary(rows)
    lines = [
        "# SDK real build/map/stack evidence report",
        "",
        "This report is generated from per-target evidence JSON files. It does not convert missing SDK toolchain access into PASS.",
        "",
        "| Status | Count |",
        "|---|---:|",
    ]
    for key in ["PASS", "FAIL", "ENVIRONMENT_BLOCKED", "NOT_RUN", "NOT_APPLICABLE"]:
        lines.append(f"| {key} | {summary.get(key, 0)} |")
    lines += ["", "| Target | Evidence | SHA manifest |", "|---|---|---|"]
    for row in rows:
        target = row["target"]
        ev = f"docs/release/sdk_evidence/{target}/evidence.json"
        sha = f"docs/release/sdk_evidence/{target}/sha256sums.txt"
        lines.append(f"| `{target}` | `{ev}` | `{sha}` |")
    lines += [
        "",
        "Impact: this closes the release-evidence gap only when actual SDK logs are committed. In toolchain-blocked environments the reports remain ENVIRONMENT_BLOCKED by design.",
        "",
    ]
    return "\n".join(lines)




def render_canonical_report(rows: list[dict[str, object]]) -> str:
    lines = [
        "# SDK canonical build evidence capture report",
        "",
        "SDK capture PASS requires `EV_SDK_BUILD_TARGET=<target>`, `EV_SDK_BUILD_STATUS=PASS`, `EV_SDK_BUILD_RC=0`, non-zero APP_BIN for buildable/HIL/physical targets and non-zero real memory evidence.",
        "",
        "`EV_MEM_REPORT_RESULT PASS` may support memory-report diagnostics but is not SDK build proof.",
        "",
        "| Target | Status | Target marker | Status marker | RC | APP_BIN | Memory | Self-test | Mixed | Reason |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        values = row.get("values", {}) or {}
        lines.append(f"| `{row['target']}` | {row.get('status')} | {row.get('target_marker_match', False)} | {row.get('build_status_marker_seen', False)} | {row.get('build_rc', '')} | {values.get('APP_BIN',0)} | {row.get('memory_nonzero', False)} | {row.get('self_test_marker_seen', False)} | {row.get('mixed_transcript_detected', False)} | {row.get('reason','')} |")
    lines += ["", "Private-repo secrets remain allowed by owner policy, but log values must be redacted.", ""]
    return "\n".join(lines)


def status_summary(rows: list[dict[str, object]]) -> dict[str, int]:
    out: dict[str, int] = {}
    for row in rows:
        out[str(row.get("status", "NOT_RUN"))] = out.get(str(row.get("status", "NOT_RUN")), 0) + 1
    return out


def capture() -> int:
    EVIDENCE_ROOT.mkdir(parents=True, exist_ok=True)
    rows = [collect_target(target) for target in parse_targets()]
    render_reports(rows)
    print("EV_SDK_EVIDENCE_CAPTURE_DONE")
    return 0


def gate() -> int:
    rows = load_evidence()
    errors = []
    blocked = []
    for row in rows:
        if row.get("class") not in REQUIRED_CLASSES:
            continue
        status = row.get("status")
        if status == "PASS":
            ev_path = EVIDENCE_ROOT / str(row["target"]) / "evidence.json"
            log_path = ROOT / str(row.get("build_log", ""))
            text = log_path.read_text(encoding="utf-8", errors="ignore") if log_path.is_file() else ""
            values = row.get("values", {}) or {}
            if not ev_path.is_file() or not log_path.is_file():
                errors.append(f"{row['target']}: PASS without committed evidence.json/build.log")
            if not row.get("target_marker_seen") or not row.get("target_marker_match"):
                errors.append(f"{row['target']}: PASS without exact EV_SDK_BUILD_TARGET marker")
            if not row.get("build_status_marker_seen"):
                errors.append(f"{row['target']}: PASS without EV_SDK_BUILD_STATUS=PASS")
            if int(row.get("build_rc") if row.get("build_rc") is not None else -1) != 0:
                errors.append(f"{row['target']}: PASS without EV_SDK_BUILD_RC=0")
            if row.get("self_test_marker_seen") or row.get("mixed_transcript_detected"):
                errors.append(f"{row['target']}: PASS from self-test/mixed transcript evidence")
            if int(values.get("APP_BIN", 0) or 0) <= 0:
                errors.append(f"{row['target']}: PASS without non-zero APP_BIN")
            if not any(int(values.get(key, 0) or 0) > 0 for key in ["IRAM", "DRAM", "BSS", "DATA", "IROM"]):
                errors.append(f"{row['target']}: PASS without non-zero real EV_MEM section")
            if text and "EV_MEM_REPORT_RESULT PASS" in text and "EV_SDK_BUILD_STATUS=PASS" not in text:
                errors.append(f"{row['target']}: memory-report PASS is the only success marker")
        elif status == "ENVIRONMENT_BLOCKED":
            blocked.append(str(row["target"]))
        else:
            errors.append(f"{row['target']}: required target status={status}")
    if errors:
        for error in errors:
            print(f"EV_SDK_EVIDENCE_GATE FAIL {error}", file=sys.stderr)
        return 1
    if blocked:
        print("EV_SDK_EVIDENCE_GATE ENVIRONMENT_BLOCKED targets=" + ",".join(blocked))
        return 77
    print("EV_SDK_EVIDENCE_GATE PASS")
    return 0


def self_test() -> int:
    targets = {t.name: t for t in parse_targets()}
    generic = targets["esp8266_generic_dev"]
    metadata = targets["adafruit_feather_huzzah_esp8266"]
    valid = (
        "EV_SDK_BUILD_TARGET=esp8266_generic_dev\n"
        "EV_SDK_BUILD_PROJECT=adapters/esp8266_rtos_sdk/targets/esp8266_generic_dev\n"
        "EV_SDK_BUILD_VARIANT=default\n"
        "EV_SDK_BUILD_BEGIN\n"
        "EV_SDK_BUILD_STATUS=PASS\n"
        "EV_SDK_BUILD_RC=0\n"
        "EV_SDK_BUILD_END\n"
        "EV_MEM_IRAM=100\nEV_MEM_DRAM=200\nEV_MEM_APP_BIN=12345\n"
        "EV_MEM_STACK_USAGE status=ok max_frame=88\n"
    )
    status, reason, checks = marker_summary(generic, valid, 0)
    assert status == "PASS", reason
    assert checks["target_marker_match"] and checks["app_bin_nonzero"] and checks["memory_nonzero"]
    assert parse_memory("EV_MEM_IRAM used=100 limit=1\nEV_MEM_APP_BIN size=123\n")["APP_BIN"] == 123
    assert parse_stack("EV_MEM_STACK_USAGE status=ok files=1 max_frame=88\n")["max_frame"] == 88
    assert redact("EV_BOARD_NET_WIFI_PASSWORD secret") == "EV_BOARD_NET_WIFI_PASSWORD <REDACTED>"

    def expect_fail(text: str, contains: str) -> None:
        st, rsn, _ = marker_summary(generic, text, 0)
        assert st == "FAIL", rsn
        assert contains in rsn, rsn

    expect_fail(valid.replace("EV_SDK_BUILD_TARGET=esp8266_generic_dev\n", ""), "missing EV_SDK_BUILD_TARGET")
    expect_fail(valid.replace("EV_SDK_BUILD_TARGET=esp8266_generic_dev", "EV_SDK_BUILD_TARGET=wemos_d1_mini"), "mismatch")
    expect_fail(valid.replace("EV_SDK_BUILD_STATUS=PASS\n", ""), "missing EV_SDK_BUILD_STATUS")
    expect_fail(valid.replace("EV_SDK_BUILD_RC=0", "EV_SDK_BUILD_RC=1"), "non-zero")
    expect_fail(valid.replace("EV_MEM_APP_BIN=12345", "EV_MEM_APP_BIN=0"), "APP_BIN")
    expect_fail(valid.replace("EV_MEM_IRAM=100\nEV_MEM_DRAM=200\n", "EV_MEM_IRAM=0\nEV_MEM_DRAM=0\n"), "memory")
    expect_fail("EV_MEM_REPORT_RESULT PASS\nEV_MEM_IRAM=100\nEV_MEM_APP_BIN=123\n", "missing EV_SDK_BUILD_TARGET")
    expect_fail("python3 tools/sdk_memory_report.py --self-test\nEV_MEM_REPORT_RESULT PASS target=self-test\nEV_MEM_IRAM=123\nEV_MEM_APP_BIN=456\nmake sdk build\n", "mixed")
    expect_fail(valid + "EV_MEM_REPORT_RESULT PASS target=self-test\n", "self-test")

    metadata_log = (
        "EV_SDK_BUILD_TARGET=adafruit_feather_huzzah_esp8266\n"
        "EV_SDK_BUILD_PROJECT=adapters/esp8266_rtos_sdk/targets/adafruit_feather_huzzah_esp8266\n"
        "EV_SDK_BUILD_VARIANT=default\nEV_SDK_BUILD_BEGIN\nEV_SDK_BUILD_STATUS=PASS\nEV_SDK_BUILD_RC=0\nEV_SDK_BUILD_END\n"
    )
    st, rsn, _ = marker_summary(metadata, metadata_log, 0)
    assert st == "PASS", rsn
    print("EV_SDK_EVIDENCE_SELF_TEST PASS")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", action="store_true")
    parser.add_argument("--summarize", action="store_true")
    parser.add_argument("--gate", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.gate:
        return gate()
    if args.summarize:
        render_reports(load_evidence())
        print("EV_SDK_EVIDENCE_SUMMARY_DONE")
        return 0
    return capture()

if __name__ == "__main__":
    raise SystemExit(main())
