#!/usr/bin/env python3
"""ESP8266 SDK target matrix checker and report generator."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from time import monotonic

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "audit"))
from sdk_warning_policy import evaluate_log  # noqa: E402

TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
REPORT_MD = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
JSONL = ROOT / "logs" / "sdk" / "sdk_build_matrix.jsonl"
ALLOWED_CLASSES = {"buildable_sdk", "hil_sdk", "metadata_only", "physical_smoke", "disabled_until_project_exists"}
BUILD_CLASSES = {"buildable_sdk", "hil_sdk", "physical_smoke"}
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")


@dataclass(frozen=True)
class Target:
    name: str
    path: str
    klass: str
    baud: str
    family: str

    @property
    def abs_path(self) -> Path:
        return ROOT / self.path


def parse_targets() -> list[Target]:
    targets: list[Target] = []
    for line_no, raw in enumerate(TARGETS_DEF.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith("#"):
            continue
        match = TARGET_RE.match(line)
        if not match:
            raise ValueError(f"invalid sdk target entry at {TARGETS_DEF}:{line_no}: {raw}")
        fields = [value.strip() for value in match.groups()]
        targets.append(Target(*fields))
    return targets


def check_targets(targets: list[Target]) -> int:
    errors: list[str] = []
    names = [target.name for target in targets]
    paths = [target.path for target in targets]
    if len(set(names)) != len(names):
        errors.append("duplicate SDK target name in config/sdk_targets.def")
    if len(set(paths)) != len(paths):
        errors.append("duplicate SDK target path in config/sdk_targets.def")

    target_dirs = sorted(p for p in (ROOT / "adapters" / "esp8266_rtos_sdk" / "targets").iterdir() if p.is_dir())
    listed = {target.abs_path.resolve() for target in targets}
    for directory in target_dirs:
        if directory.resolve() not in listed:
            errors.append(f"target directory is not listed in config/sdk_targets.def: {directory.relative_to(ROOT)}")

    for target in targets:
        if target.klass not in ALLOWED_CLASSES:
            errors.append(f"target {target.name} has invalid class {target.klass}")
        if not target.abs_path.is_dir():
            errors.append(f"target {target.name} path does not exist: {target.path}")
            continue
        if not (target.abs_path / "target_usb_uart.profile").is_file():
            errors.append(f"target {target.name} is missing target_usb_uart.profile")
        if target.klass in BUILD_CLASSES:
            for rel in ["Makefile", "sdkconfig.defaults"]:
                if not (target.abs_path / rel).is_file():
                    errors.append(f"buildable target {target.name} is missing {rel}")
            if not (target.abs_path / "main").is_dir():
                errors.append(f"buildable target {target.name} is missing main/")
        if target.klass == "metadata_only":
            if (target.abs_path / "Makefile").exists() or (target.abs_path / "main").exists():
                errors.append(f"metadata-only target {target.name} unexpectedly has SDK project files")
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        print(f"EV_SDK_MATRIX_CHECK FAIL failures={len(errors)}", file=sys.stderr)
        return 1
    print(f"EV_SDK_MATRIX_CHECK PASS targets={len(targets)} buildable={sum(t.klass in BUILD_CLASSES for t in targets)}")
    return 0


def _matrix_status(build_status: str, warning_status: str) -> str:
    if build_status in {"NOT_APPLICABLE", "NOT_RUN"}:
        return build_status
    if build_status != "PASS":
        return "FAIL"
    return "PASS" if warning_status == "PASS" else "FAIL"


def _warning_summary_from_log(log_path: Path, target_name: str) -> tuple[str, int, str]:
    try:
        text = log_path.read_text(encoding="utf-8", errors="ignore")
    except OSError as exc:
        return "ENVIRONMENT_BLOCKED", 0, str(exc)
    result = evaluate_log(text, latest=True, target=target_name, strict=True)
    return str(result["status"]), int(result.get("warning_count", 0)), str(result.get("reason", ""))


def markdown_rows(targets: list[Target], status: str = "NOT_RUN", reason: str = "not executed in this report") -> str:
    lines = [
        "# SDK build matrix report",
        "",
        "This report is machine-generatable. `PASS` is used only when the SDK build command and project warning policy both ran successfully.",
        "",
        "| Target | Class | Path | Baud | Family | Status | Build | Warning policy | Warnings | Log | Reason |",
        "|---|---|---|---:|---|---:|---:|---:|---:|---|---|",
    ]
    for target in targets:
        row_status = "NOT_APPLICABLE" if target.klass == "metadata_only" else status
        row_reason = "metadata-only target; no SDK Makefile/main project" if target.klass == "metadata_only" else reason
        warning_status = "NOT_APPLICABLE" if target.klass == "metadata_only" else "NOT_RUN"
        lines.append(
            f"| `{target.name}` | `{target.klass}` | `{target.path}` | `{target.baud}` | `{target.family}` | {row_status} | {row_status} | {warning_status} | 0 | `` | {row_reason} |"
        )
    lines.append("")
    return "\n".join(lines)


def write_not_run_report(targets: list[Target], reason: str) -> None:
    REPORT_MD.parent.mkdir(parents=True, exist_ok=True)
    REPORT_MD.write_text(markdown_rows(targets, "NOT_RUN", reason), encoding="utf-8")
    JSONL.parent.mkdir(parents=True, exist_ok=True)
    with JSONL.open("w", encoding="utf-8") as fp:
        for target in targets:
            build_status = "NOT_APPLICABLE" if target.klass == "metadata_only" else "NOT_RUN"
            warning_status = "NOT_APPLICABLE" if target.klass == "metadata_only" else "NOT_RUN"
            row = {
                "target": target.name,
                "path": target.path,
                "class": target.klass,
                "command": "sdk-build-matrix-report",
                "status": build_status,
                "build_status": build_status,
                "warning_policy_status": warning_status,
                "warning_count": 0,
                "duration_ms": 0,
                "log_path": "",
                "reason": "metadata-only target" if target.klass == "metadata_only" else reason,
            }
            fp.write(json.dumps(row, sort_keys=True) + "\n")
    print(f"wrote {REPORT_MD}")


def run_build_matrix(targets: list[Target], fw: str, include_hil: bool) -> int:
    JSONL.parent.mkdir(parents=True, exist_ok=True)
    failures = 0
    with JSONL.open("w", encoding="utf-8") as fp:
        for target in targets:
            if target.klass == "metadata_only":
                row = {"target": target.name, "path": target.path, "class": target.klass, "command": "sdk-build-one", "status": "NOT_APPLICABLE", "build_status": "NOT_APPLICABLE", "warning_policy_status": "NOT_APPLICABLE", "warning_count": 0, "duration_ms": 0, "log_path": "", "reason": "metadata-only target"}
                fp.write(json.dumps(row, sort_keys=True) + "\n")
                continue
            if target.klass == "hil_sdk" and not include_hil:
                row = {"target": target.name, "path": target.path, "class": target.klass, "command": "sdk-build-one", "status": "NOT_RUN", "build_status": "NOT_RUN", "warning_policy_status": "NOT_RUN", "warning_count": 0, "duration_ms": 0, "log_path": "", "reason": "hil_sdk excluded; set --include-hil"}
                fp.write(json.dumps(row, sort_keys=True) + "\n")
                continue
            log_dir = ROOT / "logs" / "sdk" / target.name
            log_dir.mkdir(parents=True, exist_ok=True)
            log_path = log_dir / "build.log"
            start = monotonic()
            with log_path.open("w", encoding="utf-8") as log:
                completed = subprocess.run([fw, "sdk-build-one", target.name], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, text=True)
            duration_ms = int((monotonic() - start) * 1000)
            build_status = "PASS" if completed.returncode == 0 else "FAIL"
            warning_status, warning_count, warning_reason = _warning_summary_from_log(log_path, target.name)
            status = _matrix_status(build_status, warning_status)
            reason = "" if status == "PASS" else (warning_reason or f"build_status={build_status} warning_policy_status={warning_status}")
            failures += 0 if status == "PASS" else 1
            row = {
                "target": target.name,
                "path": target.path,
                "class": target.klass,
                "command": "sdk-build-one",
                "status": status,
                "build_status": build_status,
                "warning_policy_status": warning_status,
                "warning_count": warning_count,
                "duration_ms": duration_ms,
                "log_path": str(log_path.relative_to(ROOT)),
                "reason": reason,
            }
            fp.write(json.dumps(row, sort_keys=True) + "\n")
    write_matrix_report_from_jsonl(targets)
    return 0 if failures == 0 else 1


def write_matrix_report_from_jsonl(targets: list[Target]) -> None:
    rows_by_target: dict[str, dict[str, object]] = {}
    if JSONL.exists():
        for raw in JSONL.read_text(encoding="utf-8").splitlines():
            if raw.strip():
                row = json.loads(raw)
                rows_by_target[str(row["target"])] = row
    lines = [
        "# SDK build matrix report",
        "",
        "| Target | Class | Path | Status | Build | Warning policy | Warnings | Log | Reason |",
        "|---|---|---|---:|---:|---:|---:|---|---|",
    ]
    for target in targets:
        row = rows_by_target.get(target.name, {})
        status = str(row.get("status", "NOT_RUN"))
        build_status = str(row.get("build_status", status))
        warning_status = str(row.get("warning_policy_status", "NOT_RUN"))
        warning_count = int(row.get("warning_count", 0))
        log_path = str(row.get("log_path", ""))
        reason = str(row.get("reason", ""))
        lines.append(f"| `{target.name}` | `{target.klass}` | `{target.path}` | {status} | {build_status} | {warning_status} | {warning_count} | `{log_path}` | {reason} |")
    lines.append("")
    REPORT_MD.write_text("\n".join(lines), encoding="utf-8")


def self_test() -> None:
    pass_log = "\n".join([
        "EV_SDK_BUILD_TARGET=t1",
        "EV_SDK_BUILD_BEGIN",
        "EV_SDK_BUILD_STATUS=PASS",
        "EV_SDK_BUILD_RC=0",
        "EV_SDK_BUILD_END",
    ])
    warn_log = "\n".join([
        "EV_SDK_BUILD_TARGET=t1",
        "EV_SDK_BUILD_BEGIN",
        "/work/drivers/src/example.c:1:2: warning: bad [-Wunused-function]",
        "EV_SDK_BUILD_STATUS=PASS",
        "EV_SDK_BUILD_RC=0",
        "EV_SDK_BUILD_END",
    ])
    from tempfile import TemporaryDirectory
    with TemporaryDirectory() as td:
        root = Path(td)
        p = root / "pass.log"
        w = root / "warn.log"
        p.write_text(pass_log, encoding="utf-8")
        w.write_text(warn_log, encoding="utf-8")
        assert _warning_summary_from_log(p, "t1")[:2] == ("PASS", 0)
        assert _warning_summary_from_log(w, "t1")[:2] == ("FAIL", 1)
    assert _matrix_status("PASS", "PASS") == "PASS"
    assert _matrix_status("PASS", "FAIL") == "FAIL"
    assert _matrix_status("FAIL", "PASS") == "FAIL"
    assert _matrix_status("NOT_RUN", "NOT_RUN") == "NOT_RUN"
    print("SDK_MATRIX_WARNING_POLICY_SELF_TEST PASS")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="ESP8266 SDK target matrix tool")
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    sub.add_parser("check")
    sub.add_parser("self-test")
    p_path = sub.add_parser("path")
    p_path.add_argument("target")
    p_report = sub.add_parser("report")
    p_report.add_argument("--reason", default="not executed")
    p_build = sub.add_parser("build")
    p_build.add_argument("--fw", default=str(ROOT / "tools" / "fw"))
    p_build.add_argument("--include-hil", action="store_true")
    args = parser.parse_args(argv)
    targets = parse_targets()
    if args.cmd == "list":
        for target in targets:
            print(f"{target.name}\t{target.klass}\t{target.path}\t{target.baud}\t{target.family}")
        return 0
    if args.cmd == "check":
        return check_targets(targets)
    if args.cmd == "self-test":
        self_test()
        return 0
    if args.cmd == "path":
        for target in targets:
            if target.name == args.target:
                print(target.path)
                return 0
        print(f"error: unknown SDK target: {args.target}", file=sys.stderr)
        return 1
    if args.cmd == "report":
        rc = check_targets(targets)
        if rc != 0:
            return rc
        write_not_run_report(targets, args.reason)
        return 0
    if args.cmd == "build":
        rc = check_targets(targets)
        if rc != 0:
            return rc
        return run_build_matrix(targets, args.fw, args.include_hil)
    return 2


if __name__ == "__main__":
    sys.exit(main())
