#!/usr/bin/env python3
"""Optional fuzz runner/detector for host fuzz harnesses."""
from __future__ import annotations
import shutil, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

def main() -> int:
    cc = shutil.which('clang') or shutil.which('cc')
    if not cc:
        print('EV_FUZZ_RUNNER ENVIRONMENT_BLOCKED: compiler not found')
        return 77
    probe = ROOT / 'build' / 'fuzz_probe.c'
    probe.parent.mkdir(parents=True, exist_ok=True)
    probe.write_text('int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n){(void)d;(void)n;return 0;}\n', encoding='utf-8')
    out = ROOT / 'build' / 'fuzz_probe'
    proc = subprocess.run([cc, '-fsanitize=fuzzer,address,undefined', str(probe), '-o', str(out)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        print('EV_FUZZ_RUNNER ENVIRONMENT_BLOCKED: -fsanitize=fuzzer unsupported')
        return 77
    print('EV_FUZZ_RUNNER PASS compiler supports libFuzzer')
    return 0
if __name__ == '__main__':
    raise SystemExit(main())
