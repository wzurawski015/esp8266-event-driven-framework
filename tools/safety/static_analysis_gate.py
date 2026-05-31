#!/usr/bin/env python3
"""Host static-analysis gate for portable ESP8266 framework layers.

The gate runs at least one real backend when available. It never turns a missing
analysis tool into PASS: no backend means ENVIRONMENT_BLOCKED (exit 77).
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INCLUDES = [
    "-Icore/include", "-Iactors/device/include", "-Iactors/framework/include",
    "-Icore/generated/include", "-Iruntime/include", "-Imodules/include",
    "-Idrivers/include", "-Iports/include", "-Iapps/demo/include", "-Iconfig",
]
DEFINES = ["-DEV_HOST_BUILD"]
STRICT = ["-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-O0", "-g0"]
CRITICAL_TUS = [
    "core/src/ev_msg.c",
    "core/src/ev_mailbox.c",
    "core/src/ev_lease_pool.c",
    "core/src/ev_publish.c",
    "core/src/ev_send.c",
    "core/src/ev_dispose.c",
    "runtime/src/ev_delivery_service.c",
    "runtime/src/ev_runtime_poll.c",
    "runtime/src/ev_power_state_machine.c",
    "runtime/src/ev_qos_contract.c",
]
REP_TESTS = [
    "tests/host/test_msg_contract.c",
    "tests/host/test_mailbox_contract.c",
    "tests/host/test_qos_contract_table.c",
    "tests/host/test_power_state_machine.c",
]


def run(cmd: list[str]) -> tuple[int, str]:
    proc = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return proc.returncode, proc.stdout


def gcc_supports_analyzer(cc: str) -> bool:
    rc, _ = run([cc, "-fanalyzer", "-x", "c", "-", "-fsyntax-only"],)
    return rc == 0


def backend_gcc_analyzer() -> tuple[str, bool, list[str]]:
    cc = os.environ.get("CC", "cc")
    if not shutil.which(cc):
        return "gcc-analyzer", False, [f"compiler not found: {cc}"]
    if not gcc_supports_analyzer(cc):
        return "gcc-analyzer", False, [f"{cc} does not support -fanalyzer"]
    errors: list[str] = []
    for rel in CRITICAL_TUS + REP_TESTS:
        cmd = [cc, *STRICT, "-fanalyzer", *DEFINES, *INCLUDES, "-fsyntax-only", rel]
        rc, out = run(cmd)
        if rc != 0:
            errors.append(f"{rel}: rc={rc}\n{out}")
    return "gcc-analyzer", True, errors


def backend_clang_analyze() -> tuple[str, bool, list[str]]:
    clang = os.environ.get("CLANG", "clang")
    if not shutil.which(clang):
        return "clang-analyze", False, ["clang not found"]
    errors: list[str] = []
    for rel in CRITICAL_TUS[:5]:
        cmd = [clang, "--analyze", *STRICT, *DEFINES, *INCLUDES, rel]
        rc, out = run(cmd)
        if rc != 0:
            errors.append(f"{rel}: rc={rc}\n{out}")
    return "clang-analyze", True, errors


def backend_cppcheck() -> tuple[str, bool, list[str]]:
    cppcheck = os.environ.get("CPPCHECK", "cppcheck")
    if not shutil.which(cppcheck):
        return "cppcheck", False, ["cppcheck not found"]
    cmd = [cppcheck, "--enable=warning,style,performance,portability", "--error-exitcode=1", "--inline-suppr", *CRITICAL_TUS]
    rc, out = run(cmd)
    return "cppcheck", True, ([] if rc == 0 else [out])


def backend_clang_tidy() -> tuple[str, bool, list[str]]:
    tidy = os.environ.get("CLANG_TIDY", "clang-tidy")
    if not shutil.which(tidy):
        return "clang-tidy", False, ["clang-tidy not found"]
    errors: list[str] = []
    for rel in CRITICAL_TUS[:5]:
        cmd = [tidy, "--quiet", rel, "-checks=-*,clang-analyzer-*,bugprone-*,cert-*,performance-*,portability-*", "--", *STRICT, *DEFINES, *INCLUDES]
        rc, out = run(cmd)
        if rc != 0:
            errors.append(f"{rel}: rc={rc}\n{out}")
    return "clang-tidy", True, errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", choices=["all", "gcc-analyzer", "clang-analyze", "cppcheck", "clang-tidy"], default="all")
    args = ap.parse_args()
    backends = {
        "gcc-analyzer": backend_gcc_analyzer,
        "clang-analyze": backend_clang_analyze,
        "cppcheck": backend_cppcheck,
        "clang-tidy": backend_clang_tidy,
    }
    names = list(backends) if args.backend == "all" else [args.backend]
    any_ran = False
    failures: list[str] = []
    blocked: list[str] = []
    for name in names:
        bname, ran, errs = backends[name]()
        if not ran:
            blocked.extend(f"{bname}: {msg}" for msg in errs)
            continue
        any_ran = True
        if errs:
            failures.extend(f"{bname}: {msg}" for msg in errs)
        else:
            print(f"EV_STATIC_ANALYSIS_BACKEND PASS backend={bname}")
    if failures:
        for f in failures:
            print(f"EV_STATIC_ANALYSIS_GATE FAIL {f}", file=sys.stderr)
        return 1
    if not any_ran:
        for b in blocked:
            print(f"EV_STATIC_ANALYSIS_GATE ENVIRONMENT_BLOCKED {b}")
        return 77
    print("EV_STATIC_ANALYSIS_GATE PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
