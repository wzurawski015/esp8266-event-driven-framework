#!/usr/bin/env python3
"""Aggregate real SDK/HIL evidence into one Event-Driven Reactor release gate."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config" / "eventflow_hardware_evidence.def"
REPORT = ROOT / "docs" / "release" / "eventflow_hardware_evidence_report.md"
FINAL_REPORT = ROOT / "docs" / "release" / "eventflow_final_hardware_release_report.md"  # docs/release/eventflow_final_hardware_release_report.md
ARCH_DOC = ROOT / "docs" / "architecture" / "eventflow_hardware_evidence_contract.md"
RELEASE_ARCH_DOC = ROOT / "docs" / "architecture" / "eventflow_hardware_release_contract.md"  # docs/architecture/eventflow_hardware_release_contract.md
EVIDENCE_DIR = ROOT / "docs" / "release" / "eventflow_evidence" / "current"
SOURCE_RE = re.compile(r"^\s*EV_EVENTFLOW_SOURCE\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")

@dataclass(frozen=True)
class Source:
    name: str
    kind: str
    path: str
    required: bool


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as fh:
        for chunk in iter(lambda: fh.read(65536), b''):
            h.update(chunk)
    return h.hexdigest()


def parse_manifest(text: str | None = None) -> list[Source]:
    if text is None:
        text = MANIFEST.read_text(encoding='utf-8')
    out=[]
    for raw in text.splitlines():
        m=SOURCE_RE.match(raw)
        if m:
            name, kind, path, required = [x.strip() for x in m.groups()]
            out.append(Source(name, kind, path, required == 'required'))
    if not out:
        raise RuntimeError('eventflow manifest has no sources')
    return out


def load_json(rel: str) -> tuple[dict[str, object] | None, str]:
    path = ROOT / rel
    if not path.is_file():
        return None, ''
    try:
        return json.loads(path.read_text(encoding='utf-8', errors='ignore')), sha256(path)
    except Exception:
        return None, sha256(path)


def ordered(states: list[str], required: list[str]) -> bool:
    pos=0
    for want in required:
        try:
            pos = states.index(want, pos) + 1
        except ValueError:
            return False
    return True


def source_status(src: Source) -> tuple[str, str, dict[str, object] | None, str]:
    data, path_sha = load_json(src.path)
    if data is None:
        return ('ENVIRONMENT_BLOCKED' if src.required else 'NOT_RUN', 'missing or invalid JSON evidence', None, path_sha)
    status = str(data.get('status','NOT_RUN'))
    if src.kind == 'sdk' and status == 'PASS':
        values = data.get('values', {}) if isinstance(data.get('values', {}), dict) else {}
        if not any(int(values.get(k,0) or 0)>0 for k in ['IRAM','DRAM','BSS','DATA','APP_BIN']):
            return 'FAIL','SDK PASS lacks non-zero memory evidence',data,path_sha
        blog = ROOT / str(data.get('build_log',''))
        if not blog.is_file():
            return 'FAIL','SDK PASS lacks committed build log',data,path_sha
    if src.kind == 'hil_i2c' and status == 'PASS':
        if data.get('case') != 'sda-stuck-low-containment' or not data.get('fixture_coupled', False):
            return 'FAIL','I2C PASS lacks sda-stuck-low fixture-coupled evidence',data,path_sha
        if not data.get('serial_sha256'):
            return 'FAIL','I2C PASS lacks serial SHA-256',data,path_sha
    if src.kind == 'hil_wemos_smoke' and status == 'PASS':
        if int(data.get('tick_count',0) or 0) < 3 or int(data.get('snapshot_count',0) or 0) < 3:
            return 'FAIL','Wemos smoke PASS lacks tick/snapshot sequence',data,path_sha
        if not data.get('serial_sha256'):
            return 'FAIL','Wemos smoke PASS lacks serial SHA-256',data,path_sha
    if src.kind == 'hil_wemos_deepsleep' and status == 'PASS':
        required_states=['ACTIVE','SLEEP_REQUESTED','DRAINING_RUNTIME','LOG_FLUSHING','PORTS_PREPARE_SLEEP','RTC_STATE_SAVED','ENTERING_DEEP_SLEEP']
        states = list(data.get('states', []))
        if data.get('mode') != 'deepsleep' or not ordered(states, required_states):
            return 'FAIL','Wemos deep-sleep PASS lacks required ordered state sequence',data,path_sha
        if not data.get('serial_sha256'):
            return 'FAIL','Wemos deep-sleep PASS lacks serial SHA-256',data,path_sha
    return status, str(data.get('reason','')), data, path_sha


def evaluate() -> dict[str, object]:
    rows=[]; has_fail=False; has_blocked=False
    for src in parse_manifest():
        status, reason, data, path_sha = source_status(src)
        if status == 'FAIL': has_fail=True
        if status in {'ENVIRONMENT_BLOCKED','NOT_RUN'} and src.required: has_blocked=True
        rows.append({'name':src.name,'kind':src.kind,'path':src.path,'required':src.required,'status':status,'reason':reason,'path_sha256':path_sha})
    overall = 'FAIL' if has_fail else ('ENVIRONMENT_BLOCKED' if has_blocked else 'PASS')
    return {'status':overall,'sources':rows,'event_sequence':'SDK build -> ATNEL I2C containment -> Wemos boot/runtime/tick -> sleep request -> deep-sleep enter -> wake boot'}


def write_outputs(result: dict[str, object]) -> None:
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    parsed = EVIDENCE_DIR / 'parsed.json'
    parsed.write_text(json.dumps(result, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    for report in [REPORT, FINAL_REPORT]:
        report.write_text(render_report(result, parsed, final=(report==FINAL_REPORT)), encoding='utf-8')
    ARCH_DOC.write_text(arch_text(final=False), encoding='utf-8')
    RELEASE_ARCH_DOC.write_text(arch_text(final=True), encoding='utf-8')


def render_report(result: dict[str, object], parsed: Path, *, final: bool) -> str:
    title = 'Eventflow final hardware release report' if final else 'Event-flow hardware evidence report'
    lines=[f'# {title}','', '| Field | Value |','|---|---|', f"| Status | {result['status']} |", f"| Parsed evidence | `{parsed.relative_to(ROOT).as_posix()}` |", f"| Sequence | {result.get('event_sequence','')} |", '', '| Source | Kind | Status | Required | Evidence | SHA-256 | Reason |','|---|---|---:|---:|---|---|---|']
    for row in result['sources']:
        lines.append(f"| `{row['name']}` | `{row['kind']}` | {row['status']} | {row['required']} | `{row['path']}` | `{row.get('path_sha256','')}` | {row.get('reason','')} |")
    lines += ['', 'A PASS means all required real SDK and HIL sources are present and parsed as PASS. `ENVIRONMENT_BLOCKED` is preserved when hardware or SDK evidence is missing.', '']
    return '\n'.join(lines)


def arch_text(*, final: bool) -> str:
    title = 'Eventflow hardware release contract' if final else 'Eventflow hardware evidence contract'
    return f"""# {title}

The hardware event-flow gate aggregates the asynchronous path:

```text
SDK build -> flashable target -> boot -> runtime ready -> actor tick/snapshot -> I2C hardware fault -> containment/recovery -> sleep request -> quiescence/state transitions -> deep sleep entry -> wake boot
```

Every source is listed in `config/eventflow_hardware_evidence.def`. PASS requires parsed JSON,
source SHA-256 and source-specific marker validation. Missing hardware remains
`ENVIRONMENT_BLOCKED`; it is not softened into PASS.
"""


def self_test() -> int:
    manifest='EV_EVENTFLOW_SOURCE(x, sdk, docs/x.json, required)\n'
    assert parse_manifest(manifest)[0].required
    assert ordered(['A','B','C'], ['A','C'])
    result={'status':'PASS','sources':[{'name':'x','kind':'sdk','status':'PASS','required':True,'path':'docs/x.json','path_sha256':'abc','reason':''}]}
    assert 'PASS' in render_report(result, EVIDENCE_DIR/'parsed.json', final=True)
    print('EVENTFLOW_EVIDENCE_GATE_SELF_TEST PASS')
    return 0


def main() -> int:
    ap=argparse.ArgumentParser(); ap.add_argument('--self-test', action='store_true'); ap.add_argument('--report', action='store_true'); ap.add_argument('--gate', action='store_true'); ap.add_argument('--explain', action='store_true')
    args=ap.parse_args()
    if args.self_test: return self_test()
    result=evaluate(); write_outputs(result)
    if args.explain:
        print(json.dumps(result, indent=2, sort_keys=True))
    if result['status']=='PASS':
        print('EVENTFLOW_HARDWARE_EVIDENCE_GATE PASS'); return 0
    if result['status']=='ENVIRONMENT_BLOCKED':
        print('EVENTFLOW_HARDWARE_EVIDENCE_GATE ENVIRONMENT_BLOCKED')
        return 77 if args.gate else 0
    print('EVENTFLOW_HARDWARE_EVIDENCE_GATE FAIL', file=sys.stderr); return 1

if __name__ == '__main__':
    raise SystemExit(main())
