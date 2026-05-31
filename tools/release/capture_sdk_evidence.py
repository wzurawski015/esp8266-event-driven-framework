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
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
SDK_BUILD_REPORT = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
SDK_MEMORY_REPORT = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"
SDK_STACK_REPORT = ROOT / "docs" / "release" / "sdk_stack_map_release_gates_report.md"
SDK_REAL_REPORT = ROOT / "docs" / "release" / "sdk_real_build_map_stack_evidence_report.md"
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")
MEM_RE = re.compile(r"EV_MEM_(IRAM|DRAM|BSS|DATA|IROM|APP_BIN)\s+(?:used|size)=([0-9]+)")
STACK_RE = re.compile(r"EV_MEM_STACK_USAGE\s+status=([^\s]+).*?max_frame=([0-9]+)")
APP_BIN_RE = re.compile(r"EV_(?:SDK_)?APP_BIN_BYTES=([0-9]+)|EV_MEM_APP_BIN\s+size=([0-9]+)")
STATUS_PASS_RE = re.compile(r"EV_SDK_BUILD_STATUS=PASS|EV_MEM_REPORT_RESULT PASS")
SECRET_PATTERNS = [
    re.compile(r'(EV_BOARD_NET_WIFI_PASSWORD\s+)([^\s]+)'),
    re.compile(r'(EV_BOARD_NET_WIFI_SSID\s+)([^\s]+)'),
    re.compile(r'(EV_BOARD_NET_COMMAND_TOKEN\s+)([^\s]+)'),
    re.compile(r'(WIFI_PASSWORD[=:\s]+)([^\s]+)'),
    re.compile(r'(COMMAND_TOKEN[=:\s]+)([^\s]+)'),
]
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
    out = text
    for pattern in SECRET_PATTERNS:
        out = pattern.sub(lambda m: m.group(1) + "<REDACTED>", out)
    return out


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


def write_sha_manifest(directory: Path, files: Iterable[Path]) -> None:
    lines = []
    for path in files:
        if path.exists() and path.is_file():
            lines.append(f"{sha256(path)}  {path.name}")
    (directory / "sha256sums.txt").write_text("\n".join(lines) + ("\n" if lines else ""), encoding="utf-8")


def run_build(target: Target, out_dir: Path) -> tuple[str, str]:
    """Run a real build when explicitly requested and possible.

    A default run is intentionally conservative. It records ENVIRONMENT_BLOCKED
    instead of attempting a hidden host-specific SDK build.
    """
    can_build, reason = toolchain_status()
    if not can_build:
        return "ENVIRONMENT_BLOCKED", reason
    if os.environ.get("EV_SDK_EVIDENCE_RUN_BUILDS") != "1":
        return "ENVIRONMENT_BLOCKED", "EV_SDK_EVIDENCE_RUN_BUILDS is not set; no real SDK build attempted"

    cmd = ["./tools/fw", "sdk-build-one", target.name]
    log_path = out_dir / "build.log"
    proc = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    text = redact(proc.stdout)
    log_path.write_text(text, encoding="utf-8")
    if proc.returncode != 0:
        return "FAIL", f"SDK build command failed rc={proc.returncode}"
    return "PASS", "real SDK build completed"


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

    if target.klass != "metadata_only":
        status, reason = run_build(target, out_dir)
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
            if status == "PASS" and not STATUS_PASS_RE.search(text):
                status = "FAIL"
                reason = "build output did not contain required PASS marker"
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
            })
    return rows


def render_reports(rows: list[dict[str, object]]) -> None:
    SDK_BUILD_REPORT.write_text(render_build_report(rows), encoding="utf-8")
    SDK_MEMORY_REPORT.write_text(render_memory_report(rows), encoding="utf-8")
    SDK_STACK_REPORT.write_text(render_stack_report(rows), encoding="utf-8")
    SDK_REAL_REPORT.write_text(render_real_report(rows), encoding="utf-8")


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
            if not ev_path.is_file() or not log_path.is_file() or not STATUS_PASS_RE.search(text):
                errors.append(f"{row['target']}: PASS without required evidence markers")
            if not any(int(values.get(key, 0) or 0) > 0 for key in ["IRAM", "DRAM", "BSS", "DATA", "APP_BIN"]):
                errors.append(f"{row['target']}: PASS without non-zero EV_MEM evidence")
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
    sample = "EV_MEM_IRAM used=100 limit=1 free=0 status=ok\nEV_MEM_APP_BIN size=123 limit=unchecked status=unchecked\nEV_MEM_STACK_USAGE status=ok files=1 entries=2 max_frame=88 limit=unchecked function=main qualifier=static source=main.su\n"
    values = parse_memory(sample)
    assert values["IRAM"] == 100
    assert values["APP_BIN"] == 123
    stack = parse_stack(sample)
    assert stack["status"] == "PASS" and stack["max_frame"] == 88
    assert redact("EV_BOARD_NET_WIFI_PASSWORD secret") == "EV_BOARD_NET_WIFI_PASSWORD <REDACTED>"
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
