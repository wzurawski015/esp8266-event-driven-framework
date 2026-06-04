#!/usr/bin/env python3
"""Audit ESP8266 I2C SDK-bug avoidance boundaries.

The project intentionally avoids ESP8266 RTOS SDK command-link I2C APIs in the
runtime I2C path. This checker keeps that decision executable.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORBIDDEN_TOKENS = [
    '#include "driver/i2c.h"',
    '#include <driver/i2c.h>',
    'i2c_cmd_link_create',
    'i2c_cmd_link_delete',
    'i2c_master_cmd_begin',
    'i2c_master_start',
    'i2c_master_stop',
    'i2c_master_write',
    'i2c_master_read',
    'i2c_param_config',
    'i2c_driver_install',
    'i2c_driver_delete',
]
REQUIRED_ADAPTER_TOKENS = [
    'GPIO_MODE_OUTPUT_OD',
    'EV_ESP8266_I2C_TRANSACTION_TIMEOUT_US',
    'EV_ESP8266_I2C_CLOCK_STRETCH_TIMEOUT_US',
    'EV_ESP8266_I2C_RECOVERY_PULSES',
    'ev_esp8266_i2c_stop_condition',
    'ev_esp8266_i2c_recover_bus',
    'ev_esp8266_i2c_prepare_for_sleep',
    'ev_esp8266_i2c_release_bus_lines',
]
WATCH_ROOTS = [
    'adapters/esp8266_rtos_sdk/components/ev_platform',
    'core',
    'runtime',
    'actors',
    'modules',
    'drivers',
    'apps',
    'ports',
]
ALLOWLIST_PREFIXES = [
    'docs/',
    'tools/patches/',
    'tools/audit/i2c_sdk_bug_avoidance_check.py',
]
FORBIDDEN_RUNTIME_HEAP = re.compile(
    r"\b(malloc|calloc|realloc|free|strdup|pvPortMalloc|vPortFree|heap_caps_malloc|heap_caps_free)\s*\("
)
BOOT_HEAP_ALLOWLIST = {
    'xSemaphoreCreateMutex',
}


def rel(path: Path, root: Path = ROOT) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def is_allowed_reference(path: Path, root: Path = ROOT) -> bool:
    r = rel(path, root)
    return any(r == prefix or r.startswith(prefix) for prefix in ALLOWLIST_PREFIXES)


def is_watched(path: Path, root: Path = ROOT) -> bool:
    r = rel(path, root)
    return any(r == prefix or r.startswith(prefix + '/') for prefix in WATCH_ROOTS)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def iter_files(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        current = Path(dirpath)
        parts = current.relative_to(root).parts if current != root else ()
        if any(part in {'.git', 'build', 'logs', '__pycache__'} for part in parts):
            dirnames[:] = []
            continue
        for name in filenames:
            path = current / name
            if path.suffix.lower() in {'.c', '.h', '.py', '.md', '.txt', '.patch', '.def', '.mk'} or name == 'Makefile':
                yield path


def check_forbidden_sdk_i2c(root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    for path in iter_files(root):
        if not is_watched(path, root):
            continue
        if is_allowed_reference(path, root):
            continue
        try:
            text = path.read_text(encoding='utf-8', errors='ignore')
        except OSError:
            continue
        for token in FORBIDDEN_TOKENS:
            if token in text:
                errors.append(f"{rel(path, root)}: forbidden ESP8266 SDK I2C command-link token `{token}`")
    return errors


def check_adapter_contract(root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    adapter = root / 'adapters' / 'esp8266_rtos_sdk' / 'components' / 'ev_platform' / 'ev_i2c_adapter.c'
    if not adapter.is_file():
        return [f"missing adapter: {rel(adapter, root)}"]
    text = adapter.read_text(encoding='utf-8', errors='ignore')
    code = strip_comments(text)
    for token in REQUIRED_ADAPTER_TOKENS:
        if token not in text:
            errors.append(f"{rel(adapter, root)}: missing required bounded GPIO I2C token `{token}`")
    for token in FORBIDDEN_TOKENS:
        if token in code:
            errors.append(f"{rel(adapter, root)}: forbidden SDK I2C token in adapter code `{token}`")
    for match in FORBIDDEN_RUNTIME_HEAP.finditer(code):
        name = match.group(1)
        if name in BOOT_HEAP_ALLOWLIST:
            continue
        errors.append(f"{rel(adapter, root)}: forbidden runtime heap call `{name}` in ESP8266 I2C adapter")
    if 'xSemaphoreCreateMutex' in code and 'boot-time' not in text and 'bootstrap' not in text:
        errors.append(f"{rel(adapter, root)}: boot-time mutex exception must remain documented")
    return errors


def check_docs(root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    doc = root / 'docs' / 'architecture' / 'esp8266_i2c_sdk_bug_avoidance.md'
    report = root / 'docs' / 'release' / 'i2c_sdk_bug_avoidance_report.md'
    for path in [doc, report]:
        if not path.is_file():
            errors.append(f"missing I2C SDK-bug avoidance artifact: {rel(path, root)}")
    if doc.is_file():
        text = doc.read_text(encoding='utf-8', errors='ignore')
        for phrase in ['ACK/NACK', 'STOP/release', 'bounded', 'zero-heap', 'command-link', 'xSemaphoreCreateMutex']:
            if phrase not in text:
                errors.append(f"{rel(doc, root)}: missing required phrase `{phrase}`")
    return errors


def check_makefile(root: Path = ROOT) -> list[str]:
    makefile = root / 'Makefile'
    if not makefile.is_file():
        return ['Makefile missing']
    text = makefile.read_text(encoding='utf-8', errors='ignore')
    if 'i2c-sdk-bug-avoidance-gate:' not in text:
        return ['Makefile missing i2c-sdk-bug-avoidance-gate target']
    if 'tools/audit/i2c_sdk_bug_avoidance_check.py' not in text:
        return ['Makefile i2c-sdk-bug-avoidance-gate must run tools/audit/i2c_sdk_bug_avoidance_check.py']
    return []


def run(root: Path = ROOT) -> list[str]:
    errors: list[str] = []
    errors.extend(check_forbidden_sdk_i2c(root))
    errors.extend(check_adapter_contract(root))
    errors.extend(check_docs(root))
    errors.extend(check_makefile(root))
    return errors


def self_test() -> int:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / 'adapters/esp8266_rtos_sdk/components/ev_platform').mkdir(parents=True)
        (root / 'core').mkdir()
        (root / 'docs/architecture').mkdir(parents=True)
        (root / 'docs/release').mkdir(parents=True)
        (root / 'tools/patches').mkdir(parents=True)
        (root / 'tools/audit').mkdir(parents=True)
        adapter = root / 'adapters/esp8266_rtos_sdk/components/ev_platform/ev_i2c_adapter.c'
        adapter.write_text(
            '/* boot-time xSemaphoreCreateMutex exception */\n'
            'GPIO_MODE_OUTPUT_OD\nEV_ESP8266_I2C_TRANSACTION_TIMEOUT_US\n'
            'EV_ESP8266_I2C_CLOCK_STRETCH_TIMEOUT_US\nEV_ESP8266_I2C_RECOVERY_PULSES\n'
            'ev_esp8266_i2c_stop_condition\nev_esp8266_i2c_recover_bus\n'
            'ev_esp8266_i2c_prepare_for_sleep\nev_esp8266_i2c_release_bus_lines\n'
            'xSemaphoreCreateMutex();\n',
            encoding='utf-8',
        )
        (root / 'docs/architecture/esp8266_i2c_sdk_bug_avoidance.md').write_text(
            'ACK/NACK STOP/release bounded zero-heap command-link xSemaphoreCreateMutex\n', encoding='utf-8'
        )
        (root / 'docs/release/i2c_sdk_bug_avoidance_report.md').write_text('report\n', encoding='utf-8')
        (root / 'tools/patches/0001-esp8266-i2c-stop-and-speed-fix.patch').write_text('i2c_master_cmd_begin\n', encoding='utf-8')
        (root / 'Makefile').write_text('i2c-sdk-bug-avoidance-gate:\n\tpython3 tools/audit/i2c_sdk_bug_avoidance_check.py\n', encoding='utf-8')
        assert run(root) == []
        (root / 'core/bad.c').write_text('#include <driver/i2c.h>\nvoid f(){ i2c_cmd_link_create(); }\n', encoding='utf-8')
        assert any('forbidden' in err for err in run(root))
        (root / 'core/bad.c').unlink()
        adapter.write_text(adapter.read_text(encoding='utf-8').replace('ev_esp8266_i2c_stop_condition\n', ''), encoding='utf-8')
        assert any('ev_esp8266_i2c_stop_condition' in err for err in run(root))
        adapter.write_text(adapter.read_text(encoding='utf-8') + 'malloc(1);\n', encoding='utf-8')
        assert any('heap' in err for err in run(root))
    print('i2c sdk bug avoidance checker self-test passed')
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    errors = run(ROOT)
    if errors:
        for err in errors:
            print(err)
        return 1
    print('i2c sdk bug avoidance check passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
