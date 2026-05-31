#!/usr/bin/env python3
"""Import redacted real SDK build/map/stack evidence.

This tool never converts missing local SDK logs into PASS. It imports explicit
text artifacts provided by the developer and generates compact committed
evidence under docs/release/sdk_evidence/<target>/.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
BUILD_REPORT = ROOT / "docs" / "release" / "sdk_build_matrix_report.md"
MEM_REPORT = ROOT / "docs" / "release" / "sdk_memory_matrix_report.md"
STACK_REPORT = ROOT / "docs" / "release" / "sdk_stack_map_release_gates_report.md"
IMPORT_REPORT = ROOT / "docs" / "release" / "sdk_imported_build_map_stack_evidence_report.md"
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)")
PASS_RE = re.compile(r"EV_SDK_BUILD_STATUS=PASS|EV_MEM_REPORT_RESULT PASS")
MEM_RE = re.compile(r"EV_MEM_(IRAM|DRAM|BSS|DATA|IROM|APP_BIN)\s*(?:used|size|=)\s*=?\s*([0-9]+)")
APP_BIN_RE = re.compile(r"EV_SDK_APP_BIN_BYTES=([0-9]+)")
STACK_RE = re.compile(r"EV_MEM_STACK_USAGE\s+status=([^\s]+).*?max_frame=([0-9]+)")
SECRET_PATTERNS = [
    re.compile(r"(EV_BOARD_NET_WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_WIFI_SSID\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(EV_BOARD_NET_COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(WIFI_PASSWORD\s*[=: ]\s*)(\S+)", re.I),
    re.compile(r"(COMMAND_TOKEN\s*[=: ]\s*)(\S+)", re.I),
]
FORBIDDEN_SUFFIXES = {".elf", ".bin", ".o", ".a"}
REQUIRED_CLASSES = {"buildable_sdk", "physical_smoke", "hil_sdk"}


def redact(text: str) -> str:
    out = text
    for pat in SECRET_PATTERNS:
        out = pat.sub(lambda m: m.group(1) + "<REDACTED>", out)
    return out


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as fh:
        for chunk in iter(lambda: fh.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def parse_targets() -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    for raw in TARGETS_DEF.read_text(encoding='utf-8').splitlines():
        m = TARGET_RE.match(raw)
        if m:
            name, path, klass, baud, family = [x.strip() for x in m.groups()]
            out[name] = {"target": name, "path": path, "class": klass, "baud": baud, "family": family}
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
    return {"status": "PASS" if status in {"ok", "PASS"} else "STACK_NOT_AVAILABLE", "max_frame": frame, "reason": status}


def copy_text(src: Path | None, dst: Path) -> tuple[str, str]:
    if src is None:
        dst.write_text("", encoding='utf-8')
        return "", ""
    if src.suffix.lower() in FORBIDDEN_SUFFIXES:
        raise ValueError(f"binary SDK artifact is not allowed: {src}")
    text = redact(src.read_text(encoding='utf-8', errors='ignore'))
    dst.write_text(text, encoding='utf-8')
    return text, sha256(dst)


def write_sha_manifest(out_dir: Path) -> None:
    rows = []
    for name in ["build.log", "size.log", "map_summary.txt", "stack_usage.txt", "sdkconfig.effective.txt", "evidence.json"]:
        p = out_dir / name
        if p.is_file():
            rows.append(f"{sha256(p)}  {name}")
    (out_dir / "sha256sums.txt").write_text("\n".join(rows) + "\n", encoding='utf-8')


def import_target(target: str, build_log: Path, map_file: Path | None = None, size_log: Path | None = None, stack: Path | None = None, sdkconfig: Path | None = None) -> dict[str, object]:
    targets = parse_targets()
    if target not in targets:
        raise ValueError(f"unknown SDK target: {target}")
    if not build_log.is_file():
        raise ValueError(f"build log not found: {build_log}")
    out = EVIDENCE_ROOT / target
    out.mkdir(parents=True, exist_ok=True)
    build_text, _ = copy_text(build_log, out / "build.log")
    size_text, _ = copy_text(size_log, out / "size.log") if size_log else ("", "")
    map_text, _ = copy_text(map_file, out / "map_summary.txt") if map_file else ("", "")
    stack_text, _ = copy_text(stack, out / "stack_usage.txt") if stack else ("", "")
    if sdkconfig:
        copy_text(sdkconfig, out / "sdkconfig.effective.txt")
    else:
        (out / "sdkconfig.effective.txt").write_text("sdkconfig not imported\n", encoding='utf-8')
    aggregate = "\n".join([build_text, size_text, map_text, stack_text])
    values = parse_memory(aggregate)
    stack_info = parse_stack(aggregate)
    status = "PASS" if PASS_RE.search(aggregate) and any(v > 0 for v in values.values()) else "FAIL"
    reason = "imported real SDK evidence" if status == "PASS" else "missing PASS marker or non-zero EV_MEM evidence"
    meta = targets[target]
    ev = {
        **meta,
        "status": status,
        "reason": reason,
        "build_log": f"docs/release/sdk_evidence/{target}/build.log",
        "size_log": f"docs/release/sdk_evidence/{target}/size.log",
        "map_summary": f"docs/release/sdk_evidence/{target}/map_summary.txt",
        "stack_usage": f"docs/release/sdk_evidence/{target}/stack_usage.txt",
        "sdkconfig_effective": f"docs/release/sdk_evidence/{target}/sdkconfig.effective.txt",
        "values": values,
        "stack": stack_info,
        "source_sha256": sha256(out / "build.log"),
    }
    (out / "evidence.json").write_text(json.dumps(ev, indent=2, sort_keys=True) + "\n", encoding='utf-8')
    write_sha_manifest(out)
    render_reports()
    return ev


def load_rows() -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for target, meta in parse_targets().items():
        path = EVIDENCE_ROOT / target / "evidence.json"
        if path.is_file():
            rows.append(json.loads(path.read_text(encoding='utf-8')))
        else:
            rows.append({**meta, "status": "NOT_APPLICABLE" if meta["class"] == "metadata_only" else "NOT_RUN", "reason": "missing imported evidence", "values": {}, "stack": {"status": "STACK_NOT_AVAILABLE", "max_frame": 0}, "build_log": ""})
    return rows


def render_reports() -> None:
    rows = load_rows()
    BUILD_REPORT.write_text(render_build(rows), encoding='utf-8')
    MEM_REPORT.write_text(render_mem(rows), encoding='utf-8')
    STACK_REPORT.write_text(render_stack(rows), encoding='utf-8')
    IMPORT_REPORT.write_text(render_import(rows), encoding='utf-8')


def render_build(rows: list[dict[str, object]]) -> str:
    lines=["# SDK build matrix report", "", "| Target | Class | Status | Log | Reason |", "|---|---:|---:|---|---|"]
    for r in rows:
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | `{r.get('build_log','')}` | {r.get('reason','')} |")
    lines.append("\nPASS rows require imported real SDK logs with PASS markers.\n")
    return "\n".join(lines)


def render_mem(rows: list[dict[str, object]]) -> str:
    lines=["# SDK memory matrix report", "", "| Target | Class | Status | IRAM | DRAM | BSS | DATA | APP_BIN | Stack | Reason |", "|---|---:|---:|---:|---:|---:|---:|---:|---|---|"]
    for r in rows:
        v=r.get('values',{}) or {}; st=r.get('stack',{}) or {}
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | {v.get('IRAM',0)} | {v.get('DRAM',0)} | {v.get('BSS',0)} | {v.get('DATA',0)} | {v.get('APP_BIN',0)} | {st.get('status','STACK_NOT_AVAILABLE')}:{st.get('max_frame',0)} | {r.get('reason','')} |")
    lines.append("\nStrict mode: FAIL unless required rows have real EV_MEM markers and budgets satisfied.\n")
    return "\n".join(lines)


def render_stack(rows: list[dict[str, object]]) -> str:
    lines=["# SDK stack/map release gates report", "", "| Target | Class | Status | Stack status | Max frame | Map summary |", "|---|---:|---:|---|---:|---|"]
    for r in rows:
        st=r.get('stack',{}) or {}
        lines.append(f"| `{r['target']}` | `{r['class']}` | {r['status']} | {st.get('status','STACK_NOT_AVAILABLE')} | {st.get('max_frame',0)} | `{r.get('map_summary','')}` |")
    return "\n".join(lines)+"\n"


def render_import(rows: list[dict[str, object]]) -> str:
    counts={}
    for r in rows: counts[str(r.get('status'))]=counts.get(str(r.get('status')),0)+1
    lines=["# SDK imported build/map/stack evidence report", "", "This report is generated only from imported local SDK evidence. Missing logs remain NOT_RUN/ENVIRONMENT_BLOCKED.", "", "| Status | Count |", "|---|---:|"]
    for k in ["PASS","FAIL","NOT_RUN","ENVIRONMENT_BLOCKED","NOT_APPLICABLE"]:
        lines.append(f"| {k} | {counts.get(k,0)} |")
    lines += ["", "| Target | Evidence |", "|---|---|"]
    for r in rows:
        lines.append(f"| `{r['target']}` | `docs/release/sdk_evidence/{r['target']}/evidence.json` |")
    return "\n".join(lines)+"\n"


def gate() -> int:
    rows = load_rows()
    errors=[]; blocked=[]
    for r in rows:
        if r.get('class') not in REQUIRED_CLASSES: continue
        if r.get('status') == 'PASS':
            vals = r.get('values',{}) or {}
            if not any(int(vals.get(k,0) or 0)>0 for k in ['IRAM','DRAM','BSS','DATA','APP_BIN']):
                errors.append(f"{r['target']}: PASS without non-zero EV_MEM")
        elif r.get('status') in {'NOT_RUN','ENVIRONMENT_BLOCKED'}:
            blocked.append(str(r['target']))
        else:
            errors.append(f"{r['target']}: status={r.get('status')}")
    if errors:
        for e in errors: print(f"EV_SDK_IMPORT_EVIDENCE_GATE FAIL {e}", file=sys.stderr)
        return 1
    if blocked:
        print("EV_SDK_IMPORT_EVIDENCE_GATE ENVIRONMENT_BLOCKED targets="+','.join(blocked))
        return 77
    print("EV_SDK_IMPORT_EVIDENCE_GATE PASS")
    return 0


def import_root(root: Path) -> int:
    if not root.is_dir():
        print(f"EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED import root not found: {root}")
        return 77
    imported=0
    for target in parse_targets():
        tdir=root/target
        blog=tdir/'build.log'
        if blog.is_file():
            import_target(target, blog, tdir/'map_summary.txt' if (tdir/'map_summary.txt').is_file() else None, tdir/'size.log' if (tdir/'size.log').is_file() else None, tdir/'stack_usage.txt' if (tdir/'stack_usage.txt').is_file() else None, tdir/'sdkconfig.effective.txt' if (tdir/'sdkconfig.effective.txt').is_file() else None)
            imported += 1
    if imported == 0:
        print("EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED no target build.log files in import root")
        return 77
    print(f"EV_SDK_IMPORT_EVIDENCE PASS imported={imported}")
    return 0


def self_test() -> int:
    (ROOT/'build').mkdir(exist_ok=True)
    global EVIDENCE_ROOT, BUILD_REPORT, MEM_REPORT, STACK_REPORT, IMPORT_REPORT
    old=(EVIDENCE_ROOT,BUILD_REPORT,MEM_REPORT,STACK_REPORT,IMPORT_REPORT)
    with tempfile.TemporaryDirectory(dir=str(ROOT/'build')) as td:
        root=Path(td)
        EVIDENCE_ROOT=root/'out'/'sdk_evidence'; BUILD_REPORT=root/'build.md'; MEM_REPORT=root/'mem.md'; STACK_REPORT=root/'stack.md'; IMPORT_REPORT=root/'import.md'
        sample=root/'build.log'
        sample.write_text('EV_SDK_BUILD_STATUS=PASS\nEV_MEM_IRAM used=100\nEV_MEM_DRAM used=200\nEV_MEM_APP_BIN size=12345\nEV_MEM_STACK_USAGE status=ok max_frame=88\nWIFI_PASSWORD secret\n', encoding='utf-8')
        ev=import_target('esp8266_generic_dev', sample)
        assert ev['status']=='PASS'
        assert ev['values']['IRAM']==100 and ev['values']['APP_BIN']==12345
        assert '<REDACTED>' in (EVIDENCE_ROOT/'esp8266_generic_dev'/'build.log').read_text()
        bad=root/'bad.elf'; bad.write_text('binary-ish',encoding='utf-8')
        try:
            import_target('esp8266_generic_dev', sample, bad)
            raise AssertionError('binary import accepted')
        except ValueError:
            pass
    EVIDENCE_ROOT,BUILD_REPORT,MEM_REPORT,STACK_REPORT,IMPORT_REPORT=old
    print('EV_SDK_IMPORT_EVIDENCE_SELF_TEST PASS')
    return 0


def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--gate', action='store_true')
    ap.add_argument('--summarize', action='store_true')
    ap.add_argument('--import-root', type=Path, default=None)
    ap.add_argument('--import-target')
    ap.add_argument('--build-log', type=Path)
    ap.add_argument('--map', type=Path)
    ap.add_argument('--size-log', type=Path)
    ap.add_argument('--stack', type=Path)
    ap.add_argument('--sdkconfig', type=Path)
    args=ap.parse_args()
    if args.self_test: return self_test()
    if args.gate: return gate()
    if args.summarize: render_reports(); print('EV_SDK_IMPORT_EVIDENCE_SUMMARY_DONE'); return 0
    if args.import_target:
        if not args.build_log:
            print('EV_SDK_IMPORT_EVIDENCE FAIL --build-log required for --import-target', file=sys.stderr); return 1
        import_target(args.import_target, args.build_log, args.map, args.size_log, args.stack, args.sdkconfig)
        print(f'EV_SDK_IMPORT_EVIDENCE PASS target={args.import_target}')
        return 0
    root=args.import_root or (Path(os.environ['EV_SDK_EVIDENCE_IMPORT_ROOT']) if os.environ.get('EV_SDK_EVIDENCE_IMPORT_ROOT') else None)
    if root:
        return import_root(root)
    print('EV_SDK_IMPORT_EVIDENCE ENVIRONMENT_BLOCKED: EV_SDK_EVIDENCE_IMPORT_ROOT not set')
    return 77

if __name__=='__main__':
    raise SystemExit(main())
