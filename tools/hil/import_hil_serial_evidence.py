#!/usr/bin/env python3
"""Import real ATNEL/Wemos serial evidence through strict parsers."""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
I2C = ROOT / 'docs' / 'release' / 'hil_evidence' / 'i2c' / 'current' / 'parsed.json'
SMOKE = ROOT / 'docs' / 'release' / 'hil_evidence' / 'wemos_smoke' / 'current' / 'parsed.json'
DEEP = ROOT / 'docs' / 'release' / 'hil_evidence' / 'wemos_deepsleep' / 'current' / 'parsed.json'


def run(args: list[str]) -> int:
    return subprocess.run(args, cwd=ROOT).returncode


def status(path: Path) -> str:
    if not path.is_file():
        return 'NOT_RUN'
    try:
        return str(json.loads(path.read_text(encoding='utf-8')).get('status', 'FAIL'))
    except Exception:
        return 'FAIL'


def gate() -> int:
    rows = {'atnel_i2c': status(I2C), 'wemos_smoke': status(SMOKE), 'wemos_deepsleep': status(DEEP)}
    fails = [f'{k}={v}' for k, v in rows.items() if v not in {'PASS'}]
    if fails:
        blocked = [x for x in fails if any(y in x for y in ['NOT_RUN', 'ENVIRONMENT_BLOCKED'])]
        print('EV_HIL_REAL_EVIDENCE_GATE ' + ('ENVIRONMENT_BLOCKED ' if blocked else 'FAIL ') + ','.join(fails))
        return 77 if blocked and len(blocked) == len(fails) else 1
    print('EV_HIL_REAL_EVIDENCE_GATE PASS')
    return 0


def import_all() -> int:
    allow_late = os.environ.get('EV_HIL_WEMOS_SMOKE_ALLOW_RUNTIME_ALIVE_FALLBACK', '') == '1'
    normalize_late = os.environ.get('EV_HIL_WEMOS_SMOKE_NORMALIZE', '1') != '0'
    envs = [
        ('EV_HIL_ATNEL_I2C_SERIAL_LOG', [sys.executable, 'tools/hil/parse_atnel_i2c_hil_log.py', '--log']),
        ('EV_HIL_WEMOS_SMOKE_SERIAL_LOG', [sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--log']),
        ('EV_HIL_WEMOS_DEEPSLEEP_SERIAL_LOG', [sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--deepsleep', '--log']),
    ]
    blocked: list[str] = []
    rc = 0
    for var, cmd in envs:
        val = os.environ.get(var)
        if not val:
            blocked.append(var)
            continue
        full = [*cmd, val]
        if var == 'EV_HIL_WEMOS_SMOKE_SERIAL_LOG' and allow_late:
            full.extend(['--allow-runtime-alive-fallback'])
            if normalize_late:
                full.extend(['--normalize'])
        r = run(full)
        if r != 0:
            rc = r
    if rc != 0:
        return rc
    if blocked:
        print('EV_HIL_IMPORT_ALL ENVIRONMENT_BLOCKED missing=' + ','.join(blocked))
        return 77
    print('EV_HIL_IMPORT_ALL PASS')
    return 0


def self_test() -> int:
    (ROOT / 'build').mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=str(ROOT / 'build')) as td:
        d = Path(td)
        i2c = d / 'i2c.log'
        smoke = d / 'smoke.log'
        deep = d / 'deep.log'
        late = d / 'late.log'
        i2c.write_text('''\nEV_HIL_I2C_CASE_BEGIN name=sda-stuck-low-containment\nEV_HIL_I2C_SDA_FORCE_LOW requested=1 observed=1\nEV_HIL_I2C_BUS_STATE before=idle during=stuck after=recovered\nEV_HIL_I2C_RECOVERY_BEGIN\nEV_HIL_I2C_RECOVERY_RESULT status=PASS\nEV_HIL_I2C_CASE_RESULT name=sda-stuck-low-containment status=PASS\nEV_HIL_RESULT PASS failures=0 skipped=0\n''', encoding='utf-8')
        base = '''\nEV_WEMOS_SMOKE_BOOT target=wemos_esp_wroom_02_18650\nEV_WEMOS_SMOKE_RUNTIME_READY\nEV_WEMOS_SMOKE_TICK seq=1\nEV_WEMOS_SMOKE_SNAPSHOT seq=1\nEV_WEMOS_SMOKE_TICK seq=2\nEV_WEMOS_SMOKE_SNAPSHOT seq=2\nEV_WEMOS_SMOKE_TICK seq=3\nEV_WEMOS_SMOKE_SNAPSHOT seq=3\nEV_WEMOS_SMOKE_RESULT PASS\n'''
        smoke.write_text(base, encoding='utf-8')
        late.write_text('''\nEV_WEMOS_SMOKE_TICK seq=10\nEV_WEMOS_SMOKE_SNAPSHOT seq=10\nEV_WEMOS_SMOKE_TICK seq=11\nEV_WEMOS_SMOKE_SNAPSHOT seq=11\nEV_WEMOS_SMOKE_TICK seq=12\nEV_WEMOS_SMOKE_SNAPSHOT seq=12\n''', encoding='utf-8')
        deep.write_text(base + '''\nEV_POWER_SMOKE_SLEEP_REQUEST duration_us=1000000\nEV_POWER_SMOKE_STATE ACTIVE\nEV_POWER_SMOKE_STATE SLEEP_REQUESTED\nEV_POWER_SMOKE_STATE DRAINING_RUNTIME\nEV_POWER_SMOKE_STATE LOG_FLUSHING\nEV_POWER_SMOKE_STATE PORTS_PREPARE_SLEEP\nEV_POWER_SMOKE_STATE RTC_STATE_SAVED\nEV_POWER_SMOKE_STATE ENTERING_DEEP_SLEEP\nEV_POWER_SMOKE_DEEP_SLEEP_ENTER\nEV_POWER_SMOKE_WAKE_BOOT\nEV_POWER_SMOKE_WAKE_REASON reason=timer\nEV_POWER_SMOKE_RESULT PASS\n''', encoding='utf-8')
        assert run([sys.executable, 'tools/hil/parse_atnel_i2c_hil_log.py', '--log', str(i2c), '--evidence-dir', str(d / 'out_i2c')]) == 0
        assert run([sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--log', str(smoke), '--evidence-dir', str(d / 'out_smoke')]) == 0
        assert run([sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--log', str(late), '--evidence-dir', str(d / 'out_late_strict')]) != 0
        assert run([sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--allow-runtime-alive-fallback', '--normalize', '--log', str(late), '--evidence-dir', str(d / 'out_late')]) == 0
        parsed = json.loads((d / 'out_late' / 'parsed.json').read_text(encoding='utf-8'))
        assert parsed.get('runtime_alive_fallback') is True
        assert (d / 'out_late' / 'serial.raw.log').is_file()
        assert (d / 'out_late' / 'serial.normalized.log').is_file()
        assert run([sys.executable, 'tools/hil/parse_wemos_smoke_log.py', '--deepsleep', '--log', str(deep), '--evidence-dir', str(d / 'out_deep')]) == 0
    print('EV_HIL_IMPORT_SERIAL_EVIDENCE_SELF_TEST PASS')
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--self-test', action='store_true')
    ap.add_argument('--gate', action='store_true')
    ap.add_argument('--import-all', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.gate:
        return gate()
    if args.import_all:
        return import_all()
    return import_all()


if __name__ == '__main__':
    raise SystemExit(main())
