#!/usr/bin/env python3
"""Import redacted real SDK build/map/stack evidence.

This importer is deliberately strict. A mixed terminal transcript, a parser
self-test, a placeholder path, or a memory-report self-test cannot become real
SDK PASS evidence. PASS requires target-specific SDK build markers and non-zero
build/memory evidence for buildable/HIL/physical targets.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "lib"))
from ev_redaction import redact_text
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
BUILD_REPORT = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
MEM_REPORT = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"
STACK_REPORT = ROOT / "docs" / "release" / "sdk_stack_map_release_gates_report.md"
IMPORT_REPORT = ROOT / "docs" / "release" / "sdk_imported_build_map_stack_evidence_report.md"

TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)")
MEM_RE = re.compile(r"\bEV_MEM_(IRAM|DRAM|BSS|DATA|IROM|APP_BIN)\b\s*(?:used|size|=)?\s*=?\s*([0-9]+)")
APP_BIN_RE = re.compile(r"\bEV_SDK_APP_BIN_BYTES=([0-9]+)\b")
STACK_RE = re.compile(r"\bEV_MEM_STACK_USAGE\s+status=([^\s]+).*?max_frame=([0-9]+)")
SDK_TARGET_RE = re.compile(r"\bEV_SDK_BUILD_TARGET=([A-Za-z0-9_./:-]+)\b")
SDK_STATUS_PASS_RE = re.compile(r"\bEV_SDK_BUILD_STATUS=PASS\b")
MEM_REPORT_PASS_RE = re.compile(r"\bEV_MEM_REPORT_RESULT\s+PASS\b")

SECRET_PATTERNS = [
    re.compile(r"(EV_BOARD_NET_WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_WIFI_SSID\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
]
SELF_TEST_PATTERNS = [
    re.compile(pattern, re.I)
    for pattern in [
        r"\bSELF_TEST\s+PASS\b",
        r"\bSELF_TEST_RESULT\s+PASS\b",
        r"\bSDK_IMPORT_SELF_TEST\b",
        r"\bSDK_MEMORY_SELF_TEST\b",
        r"\bEV_MEM_REPORT_RESULT\s+PASS\s+target=self-test\b",
        r"\btarget=self-test\b",
        r"\b--self-test\b",
        r"\b[A-Z0-9_]+_SELF_TEST\s+PASS\b",
        r"\bPARSER_SELF_TEST\s+PASS\b",
        r"\bWEMOS_SMOKE_LOG_PARSER_SELF_TEST\s+PASS\b",
        r"\bATNEL_I2C_HIL_LOG_PARSER_SELF_TEST\s+PASS\b",
        r"\bEVENTFLOW_EVIDENCE_GATE_SELF_TEST\s+PASS\b",
    ]
]
MIXED_TRANSCRIPT_PATTERNS = [
    re.compile(pattern, re.I)
    for pattern in [
        r"\bEV_HIL_",
        r"\bEV_WEMOS_SMOKE_",
        r"\bEV_POWER_SMOKE_",
        r"\bEVENTFLOW_",
        r"\besptool\.py\b",
        r"\bHash of data verified\.",
        r"\bpython3\s+tools/.*--self-test\b",
        r"\bmake\s+.*(?:host-test|quality-gate|perf-gate|hil-)\b",
    ]
]

FORBIDDEN_SUFFIXES = {".elf", ".bin", ".o", ".a"}
REQUIRED_CLASSES = {"buildable_sdk", "physical_smoke", "hil_sdk"}
PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)


@dataclass(frozen=True)
class EvidenceError(Exception):
    status: str
    reason: str

    def __str__(self) -> str:
        return f"{self.status}: {self.reason}"


def redact(text: str) -> str:
    return redact_text(text)


def is_placeholder_path(path: Path | str) -> bool:
    raw = str(path)
    return bool(PLACEHOLDER_RE.search(raw))


def safe_read_text(path: Path | None, *, label: str, required: bool = True) -> str:
    if path is None:
        if required:
            raise EvidenceError("ENVIRONMENT_BLOCKED", f"{label} path is missing")
        return ""
    if is_placeholder_path(path):
        raise EvidenceError("ENVIRONMENT_BLOCKED", f"placeholder path was supplied for {label}: {path}")
    if path.suffix.lower() in FORBIDDEN_SUFFIXES:
        raise EvidenceError("FAIL", f"binary SDK artifact is not allowed for {label}: {path}")
    try:
        if not path.exists():
            raise EvidenceError("ENVIRONMENT_BLOCKED", f"{label} file not found: {path}")
        if path.is_dir():
            raise EvidenceError("FAIL", f"{label} path is directory, expected log file: {path}")
        return path.read_text(encoding="utf-8", errors="ignore")
    except EvidenceError:
        raise
    except PermissionError as exc:
        raise EvidenceError("FAIL", f"unable to read {label}: {path}: {exc}") from exc
    except OSError as exc:
        raise EvidenceError("FAIL", f"unable to read {label}: {path}: {exc}") from exc


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def parse_targets() -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    for raw in TARGETS_DEF.read_text(encoding="utf-8").splitlines():
        m = TARGET_RE.match(raw)
        if m:
            name, path, klass, baud, family = [x.strip() for x in m.groups()]
            out[name] = {"target": name, "path": path, "class": klass, "baud": baud, "family": family}
    if not out:
        raise RuntimeError(f"no SDK targets parsed from {TARGETS_DEF}")
    return out


def parse_memory(text: str) -> dict[str, int]:
    vals = {"IRAM": 0, "DRAM": 0, "BSS": 0, "DATA": 0, "IROM": 0, "APP_BIN": 0}
    for m in MEM_RE.finditer(text):
        vals[m.group(1)] = int(m.group(2), 10)
    m = APP_BIN_RE.search(text)
    if m:
        vals["APP_BIN"] = int(m.group(1), 10)
    return vals


def parse_stack(text: str) -> dict[str, object]:
    m = STACK_RE.search(text)
    if not m:
        return {"status": "STACK_NOT_AVAILABLE", "max_frame": 0, "reason": "no EV_MEM_STACK_USAGE marker"}
    status, frame = m.group(1), int(m.group(2), 10)
    return {"status": "PASS" if status in {"ok", "PASS", "unchecked"} else "STACK_NOT_AVAILABLE", "max_frame": frame, "reason": status}


def write_text_artifact(src_text: str, dst: Path) -> str:
    dst.write_text(redact(src_text), encoding="utf-8")
    return sha256(dst)


def marker_summary(target: str, klass: str, aggregate: str) -> tuple[str, str, dict[str, object]]:
    targets_seen = SDK_TARGET_RE.findall(aggregate)
    distinct_targets = sorted(set(targets_seen))
    self_test_marker_seen = any(p.search(aggregate) for p in SELF_TEST_PATTERNS)
    mixed_transcript_detected = self_test_marker_seen or any(p.search(aggregate) for p in MIXED_TRANSCRIPT_PATTERNS)
    build_status_marker_seen = bool(SDK_STATUS_PASS_RE.search(aggregate))
    mem_report_pass_seen = bool(MEM_REPORT_PASS_RE.search(aggregate))
    values = parse_memory(aggregate)
    app_bin_nonzero = int(values.get("APP_BIN", 0) or 0) > 0
    memory_nonzero = any(int(values.get(k, 0) or 0) > 0 for k in ["IRAM", "DRAM", "BSS", "DATA", "IROM"])
    target_marker_seen = bool(targets_seen)
    target_marker_match = target_marker_seen and len(distinct_targets) == 1 and distinct_targets[0] == target

    checks = {
        "evidence_kind": "sdk_build",
        "target_marker_seen": target_marker_seen,
        "target_marker_count": len(targets_seen),
        "target_markers": targets_seen,
        "target_marker_match": target_marker_match,
        "build_status_marker_seen": build_status_marker_seen,
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
        failures.append(f"EV_SDK_BUILD_TARGET mismatch: expected {target}, saw {','.join(distinct_targets)}")
    if not build_status_marker_seen:
        failures.append("missing EV_SDK_BUILD_STATUS=PASS")
    if self_test_marker_seen:
        failures.append("self-test marker found in real SDK evidence")
    if mixed_transcript_detected:
        failures.append("mixed transcript detected")
    if klass in REQUIRED_CLASSES and not app_bin_nonzero:
        failures.append("APP_BIN is zero or missing for required buildable/HIL/physical target")
    if klass in REQUIRED_CLASSES and not memory_nonzero:
        failures.append("all non-APP memory markers are zero or missing")

    status = "PASS" if not failures else "FAIL"
    reason = "imported target-specific SDK build evidence" if status == "PASS" else "; ".join(failures)
    return status, reason, checks


def copy_optional(path: Path | None, dst: Path, *, label: str) -> tuple[str, str]:
    if path is None:
        dst.write_text("", encoding="utf-8")
        return "", ""
    text = safe_read_text(path, label=label, required=False)
    return text, write_text_artifact(text, dst)


def import_target(target: str, build_log: Path, map_file: Path | None = None, size_log: Path | None = None, stack: Path | None = None, sdkconfig: Path | None = None) -> dict[str, object]:
    targets = parse_targets()
    if target not in targets:
        raise EvidenceError("FAIL", f"unknown SDK target: {target}")
    meta = targets[target]
    out = EVIDENCE_ROOT / target
    out.mkdir(parents=True, exist_ok=True)

    build_text = safe_read_text(build_log, label="SDK build log")
    build_sha = write_text_artifact(build_text, out / "build.log")
    size_text, size_sha = copy_optional(size_log, out / "size.log", label="SDK size log")
    map_text, map_sha = copy_optional(map_file, out / "map_summary.txt", label="SDK map summary")
    stack_text, stack_sha = copy_optional(stack, out / "stack_usage.txt", label="SDK stack usage")
    if sdkconfig:
        sdk_text = safe_read_text(sdkconfig, label="SDK config", required=False)
        sdk_sha = write_text_artifact(sdk_text, out / "sdkconfig.effective.txt")
    else:
        (out / "sdkconfig.effective.txt").write_text("sdkconfig not imported\n", encoding="utf-8")
        sdk_sha = sha256(out / "sdkconfig.effective.txt")

    aggregate = "\n".join([build_text, size_text, map_text, stack_text])
    values = parse_memory(aggregate)
    stack_info = parse_stack(aggregate)
    status, reason, checks = marker_summary(target, meta["class"], aggregate)

    ev = {
        **meta,
        **checks,
        "status": status,
        "reason": reason,
        "build_log": f"docs/release/sdk_evidence/{target}/build.log",
        "size_log": f"docs/release/sdk_evidence/{target}/size.log",
        "map_summary": f"docs/release/sdk_evidence/{target}/map_summary.txt",
        "stack_usage": f"docs/release/sdk_evidence/{target}/stack_usage.txt",
        "sdkconfig_effective": f"docs/release/sdk_evidence/{target}/sdkconfig.effective.txt",
        "values": values,
        "stack": stack_info,
        "source_sha256": build_sha,
        "source_files": {
            "build_log_sha256": build_sha,
            "size_log_sha256": size_sha,
            "map_summary_sha256": map_sha,
            "stack_usage_sha256": stack_sha,
            "sdkconfig_effective_sha256": sdk_sha,
        },
    }
    (out / "evidence.json").write_text(json.dumps(ev, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_sha_manifest(out)
    render_reports()
    return ev


def write_sha_manifest(out_dir: Path) -> None:
    rows = []
    for name in ["build.log", "size.log", "map_summary.txt", "stack_usage.txt", "sdkconfig.effective.txt", "evidence.json", "flash.log", "flash_evidence.json"]:
        p = out_dir / name
        if p.is_file():
            rows.append(f"{sha256(p)}  {name}")
    (out_dir / "sha256sums.txt").write_text("\n".join(rows) + ("\n" if rows else ""), encoding="utf-8")


def load_rows() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for target, meta in parse_targets().items():
        path = EVIDENCE_ROOT / target / "evidence.json"
        if path.is_file():
            rows.append(json.loads(path.read_text(encoding="utf-8")))
        else:
            rows.append({**meta, "status": "NOT_APPLICABLE" if meta["class"] == "metadata_only" else "NOT_RUN", "reason": "missing imported evidence", "values": {}, "stack": {"status": "STACK_NOT_AVAILABLE", "max_frame": 0}, "build_log": ""})
    return rows


def render_reports() -> None:
    rows = load_rows()
    BUILD_REPORT.write_text(render_build(rows), encoding="utf-8")
    MEM_REPORT.write_text(render_mem(rows), encoding="utf-8")
    STACK_REPORT.write_text(render_stack(rows), encoding="utf-8")
    IMPORT_REPORT.write_text(render_import(rows), encoding="utf-8")


def render_build(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK build matrix report", "", "| Target | Class | Status | Log | Target marker | Build PASS marker | Reason |", "|---|---:|---:|---|---:|---:|---|"]
    for r in rows:
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | `{r.get('build_log','')}` | {r.get('target_marker_seen', False)} | {r.get('build_status_marker_seen', False)} | {r.get('reason','')} |")
    lines.append("\nPASS rows require target-specific `EV_SDK_BUILD_TARGET=<target>`, `EV_SDK_BUILD_STATUS=PASS`, non-zero APP_BIN and non-zero memory evidence. `EV_MEM_REPORT_RESULT PASS` alone is never sufficient.\n")
    return "\n".join(lines)


def render_mem(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK memory matrix report", "", "| Target | Class | Status | IRAM | DRAM | BSS | DATA | IROM | APP_BIN | Stack | Reason |", "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    for r in rows:
        v = r.get("values", {}) or {}; st = r.get("stack", {}) or {}
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | {v.get('IRAM',0)} | {v.get('DRAM',0)} | {v.get('BSS',0)} | {v.get('DATA',0)} | {v.get('IROM',0)} | {v.get('APP_BIN',0)} | {st.get('status','STACK_NOT_AVAILABLE')}:{st.get('max_frame',0)} | {r.get('reason','')} |")
    lines.append("\nStrict mode: FAIL unless required rows have target-specific SDK evidence and real EV_MEM markers.\n")
    return "\n".join(lines)


def render_stack(rows: list[dict[str, object]]) -> str:
    lines = ["# SDK stack/map release gates report", "", "| Target | Class | Status | Stack status | Max frame | Map summary |", "|---|---:|---:|---|---:|---|"]
    for r in rows:
        st = r.get("stack", {}) or {}
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | {st.get('status','STACK_NOT_AVAILABLE')} | {st.get('max_frame',0)} | `{r.get('map_summary','')}` |")
    return "\n".join(lines) + "\n"


def render_import(rows: list[dict[str, object]]) -> str:
    counts: dict[str, int] = {}
    for r in rows:
        counts[str(r.get("status"))] = counts.get(str(r.get("status")), 0) + 1
    lines = [
        "# SDK imported build/map/stack evidence report",
        "",
        "This report is generated only from imported local SDK evidence. Missing logs remain NOT_RUN/ENVIRONMENT_BLOCKED. Mixed terminal transcripts and self-test markers are rejected.",
        "",
        "| Status | Count |",
        "|---|---:|",
    ]
    for k in ["PASS", "FAIL", "NOT_RUN", "ENVIRONMENT_BLOCKED", "NOT_APPLICABLE"]:
        lines.append(f"| {k} | {counts.get(k,0)} |")
    lines += ["", "| Target | Evidence | Reason |", "|---|---|---|"]
    for r in rows:
        lines.append(f"| `{r['target']}` | `docs/release/sdk_evidence/{r['target']}/evidence.json` | {r.get('reason','')} |")
    return "\n".join(lines) + "\n"


def import_from_one_shot(target: str, one_shot_dir: Path) -> dict[str, object]:
    if is_placeholder_path(one_shot_dir):
        raise EvidenceError("ENVIRONMENT_BLOCKED", f"placeholder one-shot evidence directory was supplied: {one_shot_dir}")
    if not one_shot_dir.exists():
        raise EvidenceError("ENVIRONMENT_BLOCKED", f"one-shot evidence directory not found: {one_shot_dir}")
    if not one_shot_dir.is_dir():
        raise EvidenceError("FAIL", f"one-shot evidence path is not a directory: {one_shot_dir}")
    manifest = one_shot_dir / "manifest.json"
    if not manifest.is_file():
        raise EvidenceError("ENVIRONMENT_BLOCKED", f"one-shot evidence manifest not found: {manifest}")
    return import_target(
        target,
        one_shot_dir / "build.log",
        one_shot_dir / "map_summary.txt",
        one_shot_dir / "size.log",
        one_shot_dir / "stack_usage.txt",
    )


def gate() -> int:
    rows = load_rows()
    errors: list[str] = []
    blocked: list[str] = []
    for r in rows:
        if r.get("class") not in REQUIRED_CLASSES:
            continue
        target = str(r.get("target"))
        if r.get("status") == "PASS":
            vals = r.get("values", {}) or {}
            if not r.get("target_marker_seen") or not r.get("target_marker_match"):
                errors.append(f"{target}: PASS without exact EV_SDK_BUILD_TARGET marker")
            if not r.get("build_status_marker_seen"):
                errors.append(f"{target}: PASS without EV_SDK_BUILD_STATUS=PASS")
            if r.get("self_test_marker_seen") or r.get("mixed_transcript_detected"):
                errors.append(f"{target}: PASS from self-test/mixed transcript evidence")
            if int(vals.get("APP_BIN", 0) or 0) <= 0:
                errors.append(f"{target}: PASS without non-zero APP_BIN")
            if not any(int(vals.get(k, 0) or 0) > 0 for k in ["IRAM", "DRAM", "BSS", "DATA", "IROM"]):
                errors.append(f"{target}: PASS without non-zero memory sections")
        elif r.get("status") in {"NOT_RUN", "ENVIRONMENT_BLOCKED"}:
            blocked.append(target)
        else:
            errors.append(f"{target}: status={r.get('status')}")
    if errors:
        for e in errors:
            print(f"EV_SDK_IMPORT_EVIDENCE_GATE FAIL {e}", file=sys.stderr)
        return 1
    if blocked:
        print("EV_SDK_IMPORT_EVIDENCE_GATE ENVIRONMENT_BLOCKED targets=" + ",".join(blocked))
        return 77
    print("EV_SDK_IMPORT_EVIDENCE_GATE PASS")
    return 0


def import_root(root: Path) -> int:
    if is_placeholder_path(root):
        print(f"EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED placeholder import root: {root}")
        return 77
    if not root.is_dir():
        print(f"EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED import root not found: {root}")
        return 77
    imported = 0
    failed = 0
    for target in parse_targets():
        tdir = root / target
        blog = tdir / "build.log"
        if blog.is_file():
            try:
                ev = import_target(target, blog, tdir / "map_summary.txt" if (tdir / "map_summary.txt").is_file() else None, tdir / "size.log" if (tdir / "size.log").is_file() else None, tdir / "stack_usage.txt" if (tdir / "stack_usage.txt").is_file() else None, tdir / "sdkconfig.effective.txt" if (tdir / "sdkconfig.effective.txt").is_file() else None)
                imported += 1
                if ev.get("status") != "PASS":
                    failed += 1
            except EvidenceError as exc:
                failed += 1
                print(f"EV_SDK_IMPORT_EVIDENCE {exc.status} target={target} reason={exc.reason}", file=sys.stderr if exc.status == "FAIL" else sys.stdout)
    if imported == 0 and failed == 0:
        print("EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED no target build.log files in import root")
        return 77
    if failed:
        print(f"EV_SDK_IMPORT_EVIDENCE FAIL imported={imported} failed={failed}", file=sys.stderr)
        return 1
    print(f"EV_SDK_IMPORT_EVIDENCE PASS imported={imported}")
    return 0


def _expect_fail(fn, contains: str = "") -> None:
    try:
        fn()
    except EvidenceError as exc:
        if contains and contains not in exc.reason:
            raise AssertionError(f"expected reason containing {contains!r}, got {exc.reason!r}") from exc
        return
    raise AssertionError("expected EvidenceError")


def self_test() -> int:
    (ROOT / "build").mkdir(exist_ok=True)
    global EVIDENCE_ROOT, BUILD_REPORT, MEM_REPORT, STACK_REPORT, IMPORT_REPORT
    old = (EVIDENCE_ROOT, BUILD_REPORT, MEM_REPORT, STACK_REPORT, IMPORT_REPORT)
    with tempfile.TemporaryDirectory(dir=str(ROOT / "build")) as td:
        root = Path(td)
        EVIDENCE_ROOT = root / "out" / "sdk_evidence"; BUILD_REPORT = root / "build.md"; MEM_REPORT = root / "mem.md"; STACK_REPORT = root / "stack.md"; IMPORT_REPORT = root / "import.md"
        valid = root / "build.log"
        valid.write_text(
            "EV_SDK_BUILD_TARGET=esp8266_generic_dev\n"
            "EV_SDK_BUILD_STATUS=PASS\n"
            "EV_MEM_IRAM=100\nEV_MEM_DRAM=200\nEV_MEM_APP_BIN=12345\n"
            "EV_MEM_STACK_USAGE status=ok max_frame=88\n"
            "WIFI_PASSWORD secret\n",
            encoding="utf-8",
        )
        ev = import_target("esp8266_generic_dev", valid)
        assert ev["status"] == "PASS"
        assert ev["target_marker_seen"] and ev["target_marker_match"] and ev["build_status_marker_seen"]
        assert ev["values"]["IRAM"] == 100 and ev["values"]["APP_BIN"] == 12345
        assert "<REDACTED>" in (EVIDENCE_ROOT / "esp8266_generic_dev" / "build.log").read_text()

        missing_target = root / "missing-target.log"
        missing_target.write_text("EV_SDK_BUILD_STATUS=PASS\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", missing_target)
        assert ev["status"] == "FAIL" and "missing EV_SDK_BUILD_TARGET" in ev["reason"]

        mismatch = root / "mismatch.log"
        mismatch.write_text("EV_SDK_BUILD_TARGET=wemos_d1_mini\nEV_SDK_BUILD_STATUS=PASS\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", mismatch)
        assert ev["status"] == "FAIL" and "mismatch" in ev["reason"]

        missing_status = root / "missing-status.log"
        missing_status.write_text("EV_SDK_BUILD_TARGET=esp8266_generic_dev\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", missing_status)
        assert ev["status"] == "FAIL" and "missing EV_SDK_BUILD_STATUS" in ev["reason"]

        self_marker = root / "self-test.log"
        self_marker.write_text("EV_SDK_BUILD_TARGET=esp8266_generic_dev\nEV_SDK_BUILD_STATUS=PASS\nEV_MEM_REPORT_RESULT PASS target=self-test\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", self_marker)
        assert ev["status"] == "FAIL" and ev["self_test_marker_seen"]

        mixed = root / "mixed.log"
        mixed.write_text("python3 tools/sdk_memory_report.py --self-test\nEV_MEM_REPORT_RESULT PASS target=self-test\nEV_MEM_IRAM=123\nEV_MEM_DRAM=456\nEV_MEM_APP_BIN=789\nmake sdk build\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", mixed)
        assert ev["status"] == "FAIL" and ev["mixed_transcript_detected"]

        zero_app = root / "zero-app.log"
        zero_app.write_text("EV_SDK_BUILD_TARGET=esp8266_generic_dev\nEV_SDK_BUILD_STATUS=PASS\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=0\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", zero_app)
        assert ev["status"] == "FAIL" and "APP_BIN" in ev["reason"]

        all_zero = root / "all-zero.log"
        all_zero.write_text("EV_SDK_BUILD_TARGET=esp8266_generic_dev\nEV_SDK_BUILD_STATUS=PASS\nEV_MEM_IRAM=0\nEV_MEM_DRAM=0\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        ev = import_target("esp8266_generic_dev", all_zero)
        assert ev["status"] == "FAIL" and "memory" in ev["reason"]

        bad = root / "bad.elf"; bad.write_text("binary-ish", encoding="utf-8")
        _expect_fail(lambda: import_target("esp8266_generic_dev", valid, bad), "binary")
        _expect_fail(lambda: import_target("esp8266_generic_dev", Path("/path/to/build.log")), "placeholder")

        one = root / "one-shot"
        one.mkdir()
        (one / "manifest.json").write_text("{}\n", encoding="utf-8")
        (one / "build.log").write_text(valid.read_text(encoding="utf-8"), encoding="utf-8")
        (one / "size.log").write_text("EV_MEM_IRAM=100\nEV_MEM_DRAM=200\nEV_MEM_APP_BIN=12345\n", encoding="utf-8")
        (one / "map_summary.txt").write_text("EV_MEM_IROM=300\n", encoding="utf-8")
        (one / "stack_usage.txt").write_text("EV_MEM_STACK_USAGE status=ok max_frame=88\n", encoding="utf-8")
        ev = import_from_one_shot("esp8266_generic_dev", one)
        assert ev["status"] == "PASS"
        badone = root / "bad-one"
        badone.mkdir()
        (badone / "manifest.json").write_text("{}\n", encoding="utf-8")
        (badone / "build.log").write_text("EV_MEM_REPORT_RESULT PASS target=self-test\nEV_MEM_IRAM=1\nEV_MEM_APP_BIN=1\n", encoding="utf-8")
        (badone / "size.log").write_text("", encoding="utf-8")
        (badone / "map_summary.txt").write_text("", encoding="utf-8")
        (badone / "stack_usage.txt").write_text("", encoding="utf-8")
        ev = import_from_one_shot("esp8266_generic_dev", badone)
        assert ev["status"] == "FAIL" and ev["self_test_marker_seen"]
    EVIDENCE_ROOT, BUILD_REPORT, MEM_REPORT, STACK_REPORT, IMPORT_REPORT = old
    print("EV_SDK_IMPORT_EVIDENCE_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--summarize", action="store_true")
    ap.add_argument("--import-root", type=Path, default=None)
    ap.add_argument("--import-target")
    ap.add_argument("--build-log", type=Path)
    ap.add_argument("--map", type=Path)
    ap.add_argument("--size-log", type=Path)
    ap.add_argument("--stack", type=Path)
    ap.add_argument("--sdkconfig", type=Path)
    ap.add_argument("--from-one-shot-dir", type=Path)
    args = ap.parse_args()
    try:
        if args.self_test:
            return self_test()
        if args.gate:
            return gate()
        if args.summarize:
            render_reports(); print("EV_SDK_IMPORT_EVIDENCE_SUMMARY_DONE"); return 0
        if args.import_target:
            if args.from_one_shot_dir:
                ev = import_from_one_shot(args.import_target, args.from_one_shot_dir)
            else:
                if not args.build_log:
                    raise EvidenceError("FAIL", "--build-log required for --import-target unless --from-one-shot-dir is used")
                ev = import_target(args.import_target, args.build_log, args.map, args.size_log, args.stack, args.sdkconfig)
            print(f"EV_SDK_IMPORT_EVIDENCE {ev['status']} target={args.import_target} reason={ev.get('reason','')}")
            return 0 if ev["status"] == "PASS" else 1
        root = args.import_root or (Path(os.environ["EV_SDK_EVIDENCE_IMPORT_ROOT"]) if os.environ.get("EV_SDK_EVIDENCE_IMPORT_ROOT") else None)
        if root:
            return import_root(root)
        print("EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED: EV_SDK_EVIDENCE_IMPORT_ROOT not set")
        return 77
    except EvidenceError as exc:
        stream = sys.stderr if exc.status == "FAIL" else sys.stdout
        print(f"EV_SDK_IMPORT_EVIDENCE {exc.status}: {exc.reason}", file=stream)
        return 77 if exc.status == "ENVIRONMENT_BLOCKED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
