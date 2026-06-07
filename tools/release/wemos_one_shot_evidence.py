#!/usr/bin/env python3
"""Deterministic Wemos one-shot evidence capture workflow.

This tool intentionally separates build, flash and serial evidence. SDK import from a bundle uses import_sdk_evidence.py --from-one-shot-dir. Eventflow release integration uses eventflow_evidence_gate.py --one-shot-dir --one-shot-required. Terminal code 130 is CONTROLLED_MONITOR_STOP, not firmware failure and not proof of PASS by itself.  A mixed
operator transcript is never considered direct release evidence.  Hardware
operations require explicit operator intent variables so that CI/self-tests
cannot flash or monitor a board accidentally.
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
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_redaction import redact_text
TARGET_DEFAULT = "wemos_esp_wroom_02_18650"
DEFAULT_BASE = ROOT / "docs" / "release" / "wemos_one_shot_evidence" / TARGET_DEFAULT
REPORT = ROOT / "docs" / "release" / "wemos_one_shot_evidence_report.md"
ARCH_DOC = ROOT / "docs" / "architecture" / "wemos_one_shot_evidence_contract.md"
WORKFLOW_DOC = ROOT / "docs" / "release" / "wemos_one_shot_evidence_workflow.md"

SECRET_PATTERNS = [
    re.compile(r"(EV_BOARD_NET_WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_WIFI_SSID\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
]
CANONICAL_SDK_RE = re.compile(r"EV_SDK_BUILD_TARGET=(?P<target>[^\s]+).*?EV_SDK_BUILD_STATUS=(?P<status>PASS|FAIL).*?EV_SDK_BUILD_RC=(?P<rc>[0-9]+)", re.S)
PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)

@dataclass
class Stage:
    name: str
    status: str = "NOT_RUN"
    started_utc: str = ""
    ended_utc: str = ""
    duration_ms: int = 0
    log: str = ""
    sha256: str = ""
    exit_code: int | None = None
    reason: str = ""


def utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def short_sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()[:8]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def redact(text: str) -> str:
    return redact_text(text)


def is_placeholder_path(path: Path | str | None) -> bool:
    if path is None:
        return False
    return bool(PLACEHOLDER_RE.search(str(path)))


def write_text(path: Path, text: str) -> str:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(redact(text), encoding="utf-8")
    return sha256_file(path)


def rel(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def make_run_id(target: str) -> str:
    return os.environ.get("EV_WEMOS_ONE_SHOT_RUN_ID") or f"{datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')}-{target}-{short_sha(str(ROOT))}"


def run_dir_from(output_dir: Path | None, target: str, run_id: str, *, deepsleep: bool = False) -> Path:
    if output_dir is None:
        base = DEFAULT_BASE
    else:
        base = output_dir if output_dir.is_absolute() else ROOT / output_dir
    if base.name == run_id or (base.parent.name == "runs") or (base.parent.name == "deepsleep-runs"):
        return base
    sub = "deepsleep-runs" if deepsleep else "runs"
    return base / sub / run_id


def ensure_new_run_dir(run_dir: Path) -> None:
    if run_dir.exists() and os.environ.get("EV_WEMOS_ONE_SHOT_OVERWRITE") != "1":
        raise RuntimeError(f"FAIL: evidence run already exists: {run_dir}; set EV_WEMOS_ONE_SHOT_OVERWRITE=1 to overwrite")
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)


def operator_intent(target: str, *, deepsleep: bool) -> dict[str, Any]:
    return {
        "target": target,
        "flash_requested": os.environ.get("EV_WEMOS_ONE_SHOT_FLASH") == "1",
        "monitor_requested": os.environ.get("EV_WEMOS_ONE_SHOT_MONITOR") == "1",
        "deepsleep_requested": deepsleep or os.environ.get("EV_WEMOS_ONE_SHOT_DEEPSLEEP") == "1",
        "distclean_requested": os.environ.get("EV_WEMOS_ONE_SHOT_DISTCLEAN", "1") == "1",
        "operator_acknowledged_private_repo_secrets": True,
        "operator_acknowledged_no_false_pass": True,
        "flash_allowed": os.environ.get("EV_HIL_ALLOW_FLASH") == "1",
        "monitor_allowed": os.environ.get("EV_HIL_ALLOW_MONITOR") == "1",
    }


def intent_block_reason(intent: dict[str, Any], *, need_flash: bool, need_monitor: bool, need_deepsleep: bool) -> str:
    if need_flash and not (intent.get("flash_requested") and intent.get("flash_allowed")):
        return "flash requires EV_WEMOS_ONE_SHOT_FLASH=1 and EV_HIL_ALLOW_FLASH=1"
    if need_monitor and not (intent.get("monitor_requested") and intent.get("monitor_allowed")):
        return "serial monitor requires EV_WEMOS_ONE_SHOT_MONITOR=1 and EV_HIL_ALLOW_MONITOR=1"
    if need_deepsleep and not intent.get("deepsleep_requested"):
        return "deep-sleep evidence requires EV_WEMOS_ONE_SHOT_DEEPSLEEP=1"
    return ""


def run_command(name: str, argv: list[str], run_dir: Path, log_name: str, env: dict[str, str] | None = None) -> Stage:
    started = datetime.now(timezone.utc)
    st = Stage(name=name, started_utc=started.strftime("%Y-%m-%dT%H:%M:%SZ"), log=log_name)
    try:
        proc = subprocess.run(argv, cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env or os.environ.copy(), timeout=int(os.environ.get("EV_WEMOS_ONE_SHOT_STEP_TIMEOUT_SEC", "600")))
        text = proc.stdout
        st.exit_code = proc.returncode
        st.status = "PASS" if proc.returncode == 0 else ("ENVIRONMENT_BLOCKED" if proc.returncode == 77 else "FAIL")
        st.reason = "command completed" if proc.returncode == 0 else f"command exit code {proc.returncode}"
    except FileNotFoundError as exc:
        text = str(exc) + "\n"
        st.status = "ENVIRONMENT_BLOCKED"
        st.exit_code = 127
        st.reason = "command not found"
    except subprocess.TimeoutExpired as exc:
        text = (exc.stdout or "") + "\nTIMEOUT\n"
        st.status = "FAIL"
        st.exit_code = 124
        st.reason = "timeout"
    st.sha256 = write_text(run_dir / log_name, text)
    ended = datetime.now(timezone.utc)
    st.ended_utc = ended.strftime("%Y-%m-%dT%H:%M:%SZ")
    st.duration_ms = int((ended - started).total_seconds() * 1000)
    return st


def sdk_build_command(target: str) -> list[str]:
    return ["./tools/fw", "sdk-build-one", target]


def sdk_warning_policy_command(target: str, build_log: Path, output_json: Path) -> list[str]:
    return [
        sys.executable,
        "tools/audit/sdk_warning_policy.py",
        "--project-only",
        "--latest-build-session",
        "--session-kind",
        "build",
        "--strict-build-session",
        "--target",
        target,
        "--json",
        str(output_json),
        str(build_log),
    ]



def stage_dict(st: Stage) -> dict[str, Any]:
    return {
        "name": st.name,
        "status": st.status,
        "started_utc": st.started_utc,
        "ended_utc": st.ended_utc,
        "duration_ms": st.duration_ms,
        "log": st.log,
        "sha256": st.sha256,
        "exit_code": st.exit_code,
        "reason": st.reason,
    }


def parse_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return None


def write_sha_index(run_dir: Path) -> None:
    lines: list[str] = []
    for path in sorted(run_dir.iterdir()):
        if path.is_file() and path.name != "sha256sums.txt":
            lines.append(f"{sha256_file(path)}  {path.name}")
    (run_dir / "sha256sums.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")


def evaluate_manifest(manifest: dict[str, Any]) -> tuple[str, str]:
    stages = {str(s.get("name")): str(s.get("status")) for s in manifest.get("stages", [])}
    smoke = manifest.get("wemos_smoke", {}) if isinstance(manifest.get("wemos_smoke"), dict) else {}
    flash = manifest.get("flash", {}) if isinstance(manifest.get("flash"), dict) else {}
    sdk = manifest.get("sdk", {}) if isinstance(manifest.get("sdk"), dict) else {}
    intent = manifest.get("operator_intent", {}) if isinstance(manifest.get("operator_intent"), dict) else {}

    if any(status == "FAIL" for status in stages.values()):
        return "FAIL", "one or more workflow stages failed"
    flash_required = bool(intent.get("flash_requested"))
    monitor_required = bool(intent.get("monitor_requested"))
    if flash_required and flash.get("status") != "PASS":
        return "PARTIAL_EVIDENCE", "flash was requested but flash proof is not PASS"
    if monitor_required and smoke.get("status") != "PASS":
        return "PARTIAL_EVIDENCE", "monitor was requested but Wemos smoke proof is not PASS"
    if sdk.get("status") == "PASS" and (not flash_required or flash.get("status") == "PASS") and (not monitor_required or smoke.get("status") == "PASS"):
        if flash_required and monitor_required:
            return "PASS_FULL_BUILD_FLASH_SMOKE", "SDK build, flash and smoke evidence are PASS"
        if monitor_required:
            return "PASS_SMOKE_ONLY", "smoke evidence is PASS with SDK evidence present"
        return "PASS_BUILD_ONLY", "SDK build evidence is PASS"
    if smoke.get("status") == "PASS" and monitor_required and not flash_required:
        return "PASS_SMOKE_ONLY", "operator requested smoke-only evidence"
    if any(status == "ENVIRONMENT_BLOCKED" for status in stages.values()):
        return "ENVIRONMENT_BLOCKED", "one or more required stages were blocked by environment/operator intent"
    return "PARTIAL_EVIDENCE", "bundle does not satisfy full PASS requirements"


def write_manifest(run_dir: Path, manifest: dict[str, Any], *, update_release_report: bool = False) -> None:
    status, reason = evaluate_manifest(manifest)
    manifest["status"] = status
    manifest["reason"] = reason
    manifest["updated_utc"] = utc_now()
    manifest_path = run_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_sha_index(run_dir)
    render_report(run_dir, manifest, update_release_report=update_release_report)


def render_report(run_dir: Path, manifest: dict[str, Any], *, update_release_report: bool = False) -> None:
    lines = [
        "# Wemos one-shot evidence run",
        "",
        "| Field | Value |",
        "|---|---|",
        f"| Status | {manifest.get('status')} |",
        f"| Reason | {manifest.get('reason')} |",
        f"| Target | {manifest.get('target')} |",
        f"| Run ID | {manifest.get('run_id')} |",
        f"| Evidence dir | `{rel(run_dir)}` |",
        "",
        "| Stage | Status | Log | SHA-256 | Reason |",
        "|---|---:|---|---|---|",
    ]
    for st in manifest.get("stages", []):
        lines.append(f"| {st.get('name')} | {st.get('status')} | `{st.get('log','')}` | `{st.get('sha256','')}` | {st.get('reason','')} |")
    lines += [
        "",
        "This bundle keeps clean logs from the start: build, flash and serial evidence are separate files. Private repo secrets remain in the allowlisted source file and must not appear in evidence artifacts.",
        "",
    ]
    (run_dir / "excerpt.md").write_text("\n".join(lines), encoding="utf-8")
    try:
        run_rel = run_dir.relative_to(ROOT).as_posix()
    except ValueError:
        run_rel = ""
    if update_release_report:
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text("\n".join(lines), encoding="utf-8")


def run_parser(argv: list[str]) -> tuple[str, dict[str, Any]]:
    proc = subprocess.run(argv, cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    status = "PASS" if proc.returncode == 0 else ("ENVIRONMENT_BLOCKED" if proc.returncode == 77 else "FAIL")
    return status, {"argv": argv, "returncode": proc.returncode, "stdout_tail": proc.stdout[-2000:], "stderr_tail": proc.stderr[-2000:]}


def parse_sdk_build_log(log_path: Path, target: str) -> dict[str, Any]:
    text = log_path.read_text(encoding="utf-8", errors="ignore") if log_path.is_file() else ""
    match = CANONICAL_SDK_RE.search(text)
    if match is None:
        return {
            "status": "ENVIRONMENT_BLOCKED",
            "build_status": "UNKNOWN",
            "warning_policy_status": "NOT_RUN",
            "warning_count": 0,
            "log_path": log_path.name,
            "reason": "no complete EV_SDK_BUILD_BEGIN/END block found",
            "target": target,
        }
    build_status = match.group("status")
    rc = int(match.group("rc"))
    return {
        "status": "PASS" if build_status == "PASS" and rc == 0 else "FAIL",
        "build_status": build_status,
        "build_rc": rc,
        "warning_policy_status": "NOT_RUN",
        "warning_count": 0,
        "log_path": log_path.name,
        "target": match.group("target"),
        "reason": "markerized sdk-build-one evidence parsed",
    }


def update_sdk_warning_policy(run_dir: Path, target: str, sdk: dict[str, Any]) -> Stage:
    warning_log = "sdk-warning-policy.log"
    result_path = run_dir / "sdk_warning_policy.json"
    stage = run_command("SDK_WARNING_POLICY", sdk_warning_policy_command(target, run_dir / "build.log", result_path), run_dir, warning_log)
    result = parse_json(result_path) or {}
    sdk["warning_policy_status"] = stage.status
    sdk["warning_count"] = int(result.get("warning_count", 0) or 0)
    sdk["warning_policy_json_path"] = "sdk_warning_policy.json" if result_path.is_file() else ""
    sdk["session_mode"] = result.get("session_mode", "")
    sdk["status"] = "PASS" if sdk.get("build_status") == "PASS" and stage.status == "PASS" else ("ENVIRONMENT_BLOCKED" if "ENVIRONMENT_BLOCKED" in {sdk.get("status"), stage.status} else "FAIL")
    if stage.status != "PASS":
        sdk["reason"] = stage.reason
    return stage


def capture(args: argparse.Namespace, *, deepsleep: bool = False) -> int:
    target = args.target
    run_id = make_run_id(target)
    run_dir = run_dir_from(args.output_dir, target, run_id, deepsleep=deepsleep)
    ensure_new_run_dir(run_dir)
    intent = operator_intent(target, deepsleep=deepsleep)
    write_text(run_dir / "operator_intent.json", json.dumps(intent, indent=2, sort_keys=True) + "\n")

    manifest: dict[str, Any] = {
        "evidence_kind": "wemos_one_shot_bundle",
        "target": target,
        "run_id": run_id,
        "run_dir": rel(run_dir),
        "operator_intent": intent,
        "stages": [],
        "sdk": {"status": "NOT_RUN"},
        "flash": {"status": "NOT_RUN"},
        "wemos_smoke": {"status": "NOT_RUN"},
        "deep_sleep": {"status": "NOT_RUN"},
        "target_timing": {"status": "NOT_RUN"},
    }

    def add_stage(stage: Stage) -> None:
        manifest["stages"].append(stage_dict(stage))
        write_manifest(run_dir, manifest, update_release_report=bool(getattr(args, "update_report", False)))

    # Preflight never requires hardware and should write an explicit log.
    preflight_text = f"EV_WEMOS_ONE_SHOT_PREFLIGHT target={target}\noperator_intent={json.dumps(intent, sort_keys=True)}\n"
    st = Stage(name="PREFLIGHT", status="PASS", started_utc=utc_now(), ended_utc=utc_now(), log="preflight.log", reason="preflight complete")
    st.sha256 = write_text(run_dir / st.log, preflight_text)
    add_stage(st)

    secrets = run_command("SECRETS_STATUS", ["./tools/fw", "wifi-secrets-status"], run_dir, "secrets-status.log")
    add_stage(secrets)

    # Build path: safe to try. Markerized sdk-build-one gives EV_SDK_BUILD_BEGIN/END evidence; if SDK/docker is missing, mark blocked/fail honestly.
    if os.environ.get("EV_WEMOS_ONE_SHOT_DISTCLEAN", "1") == "1":
        add_stage(run_command("DISTCLEAN", ["./tools/fw", "sdk-distclean"], run_dir, "distclean.log"))
    else:
        add_stage(Stage(name="DISTCLEAN", status="NOT_RUN", started_utc=utc_now(), ended_utc=utc_now(), reason="EV_WEMOS_ONE_SHOT_DISTCLEAN=0"))
    add_stage(Stage(name="DEFCONFIG", status="NOT_RUN", started_utc=utc_now(), ended_utc=utc_now(), reason="covered by markerized sdk-build-one", log="defconfig.log"))
    build_stage = run_command("BUILD", sdk_build_command(target), run_dir, "build.log")
    add_stage(build_stage)
    manifest["sdk"] = parse_sdk_build_log(run_dir / "build.log", target)
    add_stage(update_sdk_warning_policy(run_dir, target, manifest["sdk"]))
    add_stage(Stage(name="SIZE_MAP_STACK", status="NOT_RUN", started_utc=utc_now(), ended_utc=utc_now(), reason="sdk-build-one runs sdk-memory-report inside build.log", log="size.log"))
    # Placeholders reserved for stack/map summaries. They are not PASS by themselves.
    if not (run_dir / "map_summary.txt").exists():
        write_text(run_dir / "map_summary.txt", "MAP_SUMMARY_NOT_CAPTURED reason=use SDK importer/capture on real build artifacts\n")
    if not (run_dir / "stack_usage.txt").exists():
        write_text(run_dir / "stack_usage.txt", "STACK_NOT_AVAILABLE reason=one-shot workflow did not find .su stack files\n")

    if intent_block_reason(intent, need_flash=True, need_monitor=False, need_deepsleep=False):
        reason = intent_block_reason(intent, need_flash=True, need_monitor=False, need_deepsleep=False)
        add_stage(Stage(name="FLASH", status="ENVIRONMENT_BLOCKED", started_utc=utc_now(), ended_utc=utc_now(), reason=reason, log="flash.log"))
    else:
        add_stage(run_command("FLASH", ["./tools/fw", "sdk-flash"], run_dir, "flash.log"))

    if intent_block_reason(intent, need_flash=False, need_monitor=True, need_deepsleep=deepsleep):
        reason = intent_block_reason(intent, need_flash=False, need_monitor=True, need_deepsleep=deepsleep)
        add_stage(Stage(name="SERIAL_MONITOR", status="ENVIRONMENT_BLOCKED", started_utc=utc_now(), ended_utc=utc_now(), reason=reason, log="serial.raw.log"))
    else:
        cmd = ["./tools/fw", "wemos-smoke-monitor"]
        add_stage(run_command("SERIAL_MONITOR", cmd, run_dir, "serial.raw.log"))

    # Parse flash if a log exists.
    flash_log = run_dir / "flash.log"
    if flash_log.is_file():
        status, info = run_parser([sys.executable, "tools/release/parse_esptool_flash_log.py", "--target", target, "--log", str(flash_log), "--evidence-dir", str(run_dir)])
        manifest["flash"] = parse_json(run_dir / "flash_evidence.json") or {"status": status, "parser": info}
        add_stage(Stage(name="PARSE_FLASH", status=status, started_utc=utc_now(), ended_utc=utc_now(), reason="parse_esptool_flash_log.py", log="flash_evidence.json", sha256=sha256_file(run_dir / "flash_evidence.json") if (run_dir / "flash_evidence.json").is_file() else ""))

    serial_log = run_dir / "serial.raw.log"
    if serial_log.is_file():
        if deepsleep:
            smoke_cmd = [sys.executable, "tools/hil/parse_wemos_smoke_log.py", "--deepsleep", "--log", str(serial_log), "--evidence-dir", str(run_dir)]
        else:
            smoke_cmd = [sys.executable, "tools/hil/parse_wemos_smoke_log.py", "--allow-runtime-alive-fallback", "--normalize", "--log", str(serial_log), "--evidence-dir", str(run_dir)]
        status, info = run_parser(smoke_cmd)
        parsed = parse_json(run_dir / "parsed.json") or {"status": status, "parser": info}
        if deepsleep:
            manifest["deep_sleep"] = parsed
        else:
            manifest["wemos_smoke"] = parsed
        add_stage(Stage(name="PARSE_SMOKE" if not deepsleep else "PARSE_DEEPSLEEP", status=status, started_utc=utc_now(), ended_utc=utc_now(), reason="parse_wemos_smoke_log.py", log="parsed.json", sha256=sha256_file(run_dir / "parsed.json") if (run_dir / "parsed.json").is_file() else ""))

        timing_cmd = [sys.executable, "tools/perf/parse_esp8266_target_timing.py", "--from-one-shot-dir", str(run_dir), "--target", target]
        timing_status, timing_info = run_parser(timing_cmd)
        timing_json = parse_json(run_dir / "target_timing.json") or {"status": timing_status, "parser": timing_info}
        metrics = timing_json.get("metrics", {}) if isinstance(timing_json.get("metrics"), dict) else {}
        tick = metrics.get("smoke_tick_interval", {}) if isinstance(metrics.get("smoke_tick_interval"), dict) else {}
        manifest["target_timing"] = {
            "status": timing_json.get("status", timing_status),
            "path": "target_timing.json" if (run_dir / "target_timing.json").is_file() else "",
            "sha256": sha256_file(run_dir / "target_timing.json") if (run_dir / "target_timing.json").is_file() else "",
            "samples": timing_json.get("sample_count", 0),
            "p99_ms": timing_json.get("p99_ms") if timing_json.get("p99_ms") is not None else tick.get("p99_ms"),
            "p999_ms": timing_json.get("p999_ms") if timing_json.get("p999_ms") is not None else tick.get("p999_ms"),
        }
        add_stage(Stage(name="PARSE_TARGET_TIMING", status=timing_status, started_utc=utc_now(), ended_utc=utc_now(), reason="parse_esp8266_target_timing.py", log="target_timing.json", sha256=sha256_file(run_dir / "target_timing.json") if (run_dir / "target_timing.json").is_file() else ""))

    # SDK strict import from bundle is a later patch; patch 0001 records bundle status only.
    write_manifest(run_dir, manifest, update_release_report=bool(getattr(args, "update_report", False)))
    print(f"EV_WEMOS_ONE_SHOT_EVIDENCE {manifest['status']} dir={rel(run_dir)} reason={manifest['reason']}")
    if manifest["status"] == "FAIL":
        return 1
    if manifest["status"] == "ENVIRONMENT_BLOCKED":
        return 77
    return 0


def gate(args: argparse.Namespace) -> int:
    d = args.evidence_dir or DEFAULT_BASE / "current"
    d = d if d.is_absolute() else ROOT / d
    manifest_path = d / "manifest.json"
    if not manifest_path.is_file():
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_GATE ENVIRONMENT_BLOCKED: manifest not found: {manifest_path}")
        return 77
    manifest = json.loads(manifest_path.read_text(encoding="utf-8", errors="ignore"))
    status = str(manifest.get("status", "FAIL"))
    if status.startswith("PASS"):
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_GATE PASS status={status}")
        return 0
    if status in {"ENVIRONMENT_BLOCKED", "NOT_RUN", "PARTIAL_EVIDENCE"}:
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_GATE {status}: {manifest.get('reason','')}")
        return 77
    print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_GATE FAIL: {manifest.get('reason','')}", file=sys.stderr)
    return 1


def explain(args: argparse.Namespace) -> int:
    d = args.evidence_dir or DEFAULT_BASE / "current"
    d = d if d.is_absolute() else ROOT / d
    path = d / "manifest.json"
    if not path.is_file():
        print(json.dumps({"status": "ENVIRONMENT_BLOCKED", "reason": f"manifest not found: {path}"}, indent=2))
        return 77
    print(path.read_text(encoding="utf-8", errors="ignore"))
    return 0


def preflight(args: argparse.Namespace) -> int:
    target = args.target
    intent = operator_intent(target, deepsleep=False)
    print(json.dumps({"status": "PASS", "target": target, "operator_intent": intent, "private_repo_secrets_accepted": True}, indent=2, sort_keys=True))
    return 0


def update_report(args: argparse.Namespace) -> int:
    d = args.evidence_dir or DEFAULT_BASE / "current"
    d = d if d.is_absolute() else ROOT / d
    manifest_path = d / "manifest.json"
    if not manifest_path.is_file():
        REPORT.write_text(
            "# Wemos one-shot evidence run\n\n"
            "| Field | Value |\n|---|---|\n"
            "| Status | ENVIRONMENT_BLOCKED |\n"
            f"| Reason | no committed Wemos one-shot manifest at `{rel(manifest_path)}` |\n"
            f"| Evidence dir | `{rel(d)}` |\n\n"
            "Tooling is available, but real PASS requires a committed manifest-backed evidence bundle. "
            "Self-tests and temporary `test-run` directories are not release evidence. Private repo secrets remain in the allowlisted source file and must not appear in evidence artifacts.\n",
            encoding="utf-8",
        )
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_REPORT ENVIRONMENT_BLOCKED: manifest not found: {manifest_path}")
        return 77
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8", errors="ignore"))
    except Exception as exc:
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_REPORT FAIL: invalid manifest: {exc}", file=sys.stderr)
        return 1
    render_report(d, manifest, update_release_report=True)
    print(f"EV_WEMOS_ONE_SHOT_EVIDENCE_REPORT UPDATED status={manifest.get('status','UNKNOWN')}")
    return 0


def self_test() -> int:
    with tempfile.TemporaryDirectory(dir=str(ROOT / "build" if (ROOT / "build").is_dir() else ROOT)) as td:
        root = Path(td)
        run = root / "runs" / "test-run"
        ensure_new_run_dir(run)
        intent = {"flash_requested": True, "monitor_requested": True, "operator_acknowledged_private_repo_secrets": True, "operator_acknowledged_no_false_pass": True}
        write_text(run / "operator_intent.json", json.dumps(intent))
        assert sdk_build_command(TARGET_DEFAULT) == ["./tools/fw", "sdk-build-one", TARGET_DEFAULT]
        warning_cmd = sdk_warning_policy_command(TARGET_DEFAULT, run / "build.log", run / "sdk_warning_policy.json")
        assert "--strict-build-session" in warning_cmd
        assert TARGET_DEFAULT in warning_cmd
        flash = """esptool.py v3.3\nChip is ESP8266EX\nWriting at 0x00000000... (100 %)\nHash of data verified.\nLeaving...\nHard resetting via RTS pin...\n"""
        serial = """EV_WEMOS_SMOKE_TICK seq=1\nEV_WEMOS_SMOKE_SNAPSHOT seq=1\nEV_WEMOS_SMOKE_TICK seq=2\nEV_WEMOS_SMOKE_SNAPSHOT seq=2\nEV_WEMOS_SMOKE_TICK seq=3\nEV_WEMOS_SMOKE_SNAPSHOT seq=3\n^C\n--- exit ---\n[process exited with code 130 (0x00000082)]\n"""
        write_text(run / "flash.log", flash)
        write_text(run / "serial.raw.log", serial)
        status, _ = run_parser([sys.executable, "tools/release/parse_esptool_flash_log.py", "--target", TARGET_DEFAULT, "--log", str(run / "flash.log"), "--evidence-dir", str(run)])
        assert status == "PASS"
        status, _ = run_parser([sys.executable, "tools/hil/parse_wemos_smoke_log.py", "--allow-runtime-alive-fallback", "--normalize", "--log", str(run / "serial.raw.log"), "--evidence-dir", str(run)])
        assert status == "PASS"
        flash_json = parse_json(run / "flash_evidence.json") or {}
        smoke_json = parse_json(run / "parsed.json") or {}
        build_log = "\n".join([
            "EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650",
            "EV_SDK_BUILD_PROJECT=adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650",
            "EV_SDK_BUILD_VARIANT=default",
            "EV_SDK_BUILD_BEGIN",
            "EV_SDK_BUILD_STATUS=PASS",
            "EV_SDK_BUILD_RC=0",
            "EV_SDK_BUILD_END",
        ])
        write_text(run / "build.log", build_log)
        sdk_info = parse_sdk_build_log(run / "build.log", TARGET_DEFAULT)
        assert sdk_info["build_status"] == "PASS"
        bad_info = parse_sdk_build_log(run / "missing-markers.log", TARGET_DEFAULT)
        assert bad_info["status"] == "ENVIRONMENT_BLOCKED"
        manifest = {"target": TARGET_DEFAULT, "run_id": "test-run", "operator_intent": intent, "stages": [], "sdk": sdk_info, "flash": flash_json, "wemos_smoke": smoke_json, "deep_sleep": {"status": "NOT_RUN"}, "target_timing": {"status": "PASS", "path": "target_timing.json", "samples": 10}}
        write_manifest(run, manifest)
        assert (run / "manifest.json").is_file()
        assert "PASS" in (run / "manifest.json").read_text(encoding="utf-8")
        # Missing operator intent for flash/monitor must block.
        blocked = operator_intent(TARGET_DEFAULT, deepsleep=False)
        assert intent_block_reason(blocked, need_flash=True, need_monitor=False, need_deepsleep=False)
        assert intent_block_reason(blocked, need_flash=False, need_monitor=True, need_deepsleep=False)
        assert intent_block_reason(blocked, need_flash=False, need_monitor=True, need_deepsleep=True)
        ds = root / "deepsleep"
        ds.mkdir()
        deep_log = """EV_WEMOS_SMOKE_BOOT target=wemos_esp_wroom_02_18650\nEV_WEMOS_SMOKE_RUNTIME_READY source=runtime_app\nEV_WEMOS_SMOKE_TICK seq=1\nEV_WEMOS_SMOKE_SNAPSHOT seq=1\nEV_WEMOS_SMOKE_TICK seq=2\nEV_WEMOS_SMOKE_SNAPSHOT seq=2\nEV_WEMOS_SMOKE_TICK seq=3\nEV_WEMOS_SMOKE_SNAPSHOT seq=3\nEV_WEMOS_SMOKE_RESULT PASS failures=0 skipped=0 mode=firmware_runtime_alive\nEV_POWER_SMOKE_SLEEP_REQUEST duration_us=5000000\nEV_POWER_SMOKE_STATE ACTIVE\nEV_POWER_SMOKE_STATE SLEEP_REQUESTED\nEV_POWER_SMOKE_STATE DRAINING_RUNTIME\nEV_POWER_SMOKE_STATE LOG_FLUSHING\nEV_POWER_SMOKE_STATE PORTS_PREPARE_SLEEP\nEV_POWER_SMOKE_STATE RTC_STATE_SAVED\nEV_POWER_SMOKE_STATE ENTERING_DEEP_SLEEP\nEV_POWER_SMOKE_DEEP_SLEEP_ENTER\nEV_POWER_SMOKE_WAKE_BOOT\nEV_POWER_SMOKE_WAKE_REASON reason=timer\nEV_POWER_SMOKE_RESULT PASS\n"""
        (ds / "serial.raw.log").write_text(deep_log, encoding="utf-8")
        status, _ = run_parser([sys.executable, "tools/hil/parse_wemos_smoke_log.py", "--deepsleep", "--log", str(ds / "serial.raw.log"), "--evidence-dir", str(ds)])
        assert status == "PASS"
        parsed_ds = parse_json(ds / "parsed.json") or {}
        assert parsed_ds.get("mode") == "deepsleep" and not parsed_ds.get("runtime_alive_fallback")
        # Existing run should fail without overwrite.
        try:
            ensure_new_run_dir(run)
            raise AssertionError("existing run did not fail")
        except RuntimeError:
            pass
    print("WEMOS_ONE_SHOT_EVIDENCE_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--preflight", action="store_true")
    ap.add_argument("--capture", action="store_true")
    ap.add_argument("--capture-deepsleep", action="store_true")
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--explain", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--update-report", action="store_true")
    ap.add_argument("--target", default=TARGET_DEFAULT)
    ap.add_argument("--output-dir", type=Path)
    ap.add_argument("--evidence-dir", type=Path)
    args = ap.parse_args()
    try:
        if args.self_test:
            return self_test()
        if args.preflight:
            return preflight(args)
        if args.capture:
            return capture(args, deepsleep=False)
        if args.capture_deepsleep:
            return capture(args, deepsleep=True)
        if args.gate:
            return gate(args)
        if args.explain:
            return explain(args)
        if args.report:
            return update_report(args)
        ap.print_help()
        return 2
    except RuntimeError as exc:
        print(f"EV_WEMOS_ONE_SHOT_EVIDENCE FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
