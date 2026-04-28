#!/usr/bin/env python3
"""ESP8266 SDK ELF memory report helper.

The tool intentionally reports only information that can be derived from the
built ELF/section table. It does not parse compiler command lines and does not
print build flags, so local WiFi credentials passed through private build
configuration are not leaked into the report.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

SECTION_LINE_RE = re.compile(r"^\s*(\S+)\s+(\d+)\s+(?:0x[0-9A-Fa-f]+|\d+)\s*$")

IRAM_PREFIXES = (
    ".iram",
    ".iram0",
    ".iram1",
)
DRAM_PREFIXES = (
    ".dram",
    ".dram0",
    ".data",
    ".bss",
    ".noinit",
)


def parse_size_output(text: str) -> Dict[str, int]:
    sections: Dict[str, int] = {}
    for line in text.splitlines():
        match = SECTION_LINE_RE.match(line)
        if not match:
            continue
        name = match.group(1)
        if name.lower() == "total":
            continue
        size = int(match.group(2), 10)
        sections[name] = sections.get(name, 0) + size
    return sections


def section_total(sections: Dict[str, int], prefixes: Iterable[str]) -> int:
    total = 0
    for name, size in sections.items():
        if any(name.startswith(prefix) for prefix in prefixes):
            total += size
    return total


def find_app_elf(project_dir: Path) -> Path:
    build_dir = project_dir / "build"
    if not build_dir.is_dir():
        raise FileNotFoundError(f"build directory not found: {build_dir}")

    direct_candidates = sorted(build_dir.glob("*.elf"))
    if direct_candidates:
        return direct_candidates[0]

    candidates = []
    for path in build_dir.rglob("*.elf"):
        parts = set(path.parts)
        if "bootloader" in parts:
            continue
        candidates.append(path)
    candidates.sort()
    if candidates:
        return candidates[0]

    raise FileNotFoundError(f"application ELF not found under: {build_dir}")


def run_size_tool(size_tool: str, elf_path: Path) -> str:
    completed = subprocess.run(
        [size_tool, "-A", str(elf_path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        stderr = completed.stderr.strip()
        raise RuntimeError(f"{size_tool} -A failed for {elf_path}: {stderr}")
    return completed.stdout


def env_int(name: str) -> Optional[int]:
    value = os.environ.get(name, "").strip()
    if not value:
        return None
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise ValueError(f"{name} must be an integer, got {value!r}") from exc
    if parsed < 0:
        raise ValueError(f"{name} must be non-negative, got {parsed}")
    return parsed


def status_for_limit(used: int, limit: Optional[int]) -> Tuple[str, str, str, int]:
    if limit is None:
        return "unchecked", "unchecked", "unknown", 0
    free = limit - used
    if free < 0:
        return "fail", str(limit), str(free), 1
    return "ok", str(limit), str(free), 0


def emit_report(target: str, elf: str, sections: Dict[str, int]) -> int:
    failures = 0
    warnings = 0

    iram_used = section_total(sections, IRAM_PREFIXES)
    dram_used = section_total(sections, DRAM_PREFIXES)
    bss_size = section_total(sections, (".bss", ".dram0.bss"))
    data_size = section_total(sections, (".data", ".dram0.data"))

    iram_status, iram_limit, iram_free, iram_fail = status_for_limit(iram_used, env_int("EV_SDK_IRAM_LIMIT_BYTES"))
    dram_status, dram_limit, dram_free, dram_fail = status_for_limit(dram_used, env_int("EV_SDK_DRAM_LIMIT_BYTES"))
    bss_status, bss_limit, _bss_free, bss_fail = status_for_limit(bss_size, env_int("EV_SDK_MAX_BSS_BYTES"))
    data_status, data_limit, _data_free, data_fail = status_for_limit(data_size, env_int("EV_SDK_MAX_DATA_BYTES"))

    failures += iram_fail + dram_fail + bss_fail + data_fail

    if iram_status == "unchecked":
        warnings += 1
    if dram_status == "unchecked":
        warnings += 1

    print(f"EV_MEM_REPORT_START target={target} elf={elf}")
    for name in sorted(sections):
        print(f"EV_MEM_SECTION name={name} size={sections[name]}")
    print(f"EV_MEM_IRAM used={iram_used} limit={iram_limit} free={iram_free} status={iram_status}")
    print(f"EV_MEM_DRAM used={dram_used} limit={dram_limit} free={dram_free} status={dram_status}")
    print(f"EV_MEM_BSS size={bss_size} limit={bss_limit} status={bss_status}")
    print(f"EV_MEM_DATA size={data_size} limit={data_limit} status={data_status}")
    print("EV_MEM_STACK status=not_available source=elf_section_report")

    result = "PASS" if failures == 0 else "FAIL"
    print(f"EV_MEM_REPORT_RESULT {result} failures={failures} warnings={warnings}")
    return 0 if failures == 0 else 1


SELF_TEST_SIZE_OUTPUT = """
section             size         addr
.iram0.text         1200   0x40100000
.dram0.data          256   0x3ffe8000
.dram0.bss           512   0x3ffe8100
.rodata              128   0x40200000
Total               1896
"""


def self_test() -> int:
    sections = parse_size_output(SELF_TEST_SIZE_OUTPUT)
    assert sections[".iram0.text"] == 1200
    assert sections[".dram0.data"] == 256
    assert sections[".dram0.bss"] == 512
    assert section_total(sections, IRAM_PREFIXES) == 1200
    assert section_total(sections, DRAM_PREFIXES) == 768

    old_env = dict(os.environ)
    try:
        os.environ["EV_SDK_IRAM_LIMIT_BYTES"] = "1200"
        os.environ["EV_SDK_DRAM_LIMIT_BYTES"] = "768"
        assert emit_report("self-test", "self-test.elf", sections) == 0
        os.environ["EV_SDK_MAX_BSS_BYTES"] = "511"
        assert emit_report("self-test", "self-test.elf", sections) == 1
    finally:
        os.environ.clear()
        os.environ.update(old_env)

    print("EV_MEM_SELF_TEST PASS")
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description="Report ESP8266 SDK ELF memory sections with EV_MEM markers.")
    parser.add_argument("--project-dir", help="ESP8266 SDK project directory containing build/.")
    parser.add_argument("--elf", help="Explicit application ELF path.")
    parser.add_argument("--size-tool", default="xtensa-lx106-elf-size", help="Tool used to run '<tool> -A <elf>'.")
    parser.add_argument("--size-output", help="Parse section sizes from a pre-captured size -A output file.")
    parser.add_argument("--self-test", action="store_true", help="Run parser and threshold self-test.")
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    if args.size_output:
        size_text = Path(args.size_output).read_text(encoding="utf-8")
        sections = parse_size_output(size_text)
        target = args.project_dir or "size-output"
        elf = args.elf or args.size_output
        return emit_report(target, elf, sections)

    if args.elf:
        elf_path = Path(args.elf)
        project_dir = Path(args.project_dir) if args.project_dir else elf_path.parent.parent
    else:
        if not args.project_dir:
            print("error: --project-dir is required unless --elf or --size-output is provided", file=sys.stderr)
            return 2
        project_dir = Path(args.project_dir)
        elf_path = find_app_elf(project_dir)

    size_text = run_size_tool(args.size_tool, elf_path)
    sections = parse_size_output(size_text)
    if not sections:
        print(f"error: no sections parsed from {args.size_tool} output for {elf_path}", file=sys.stderr)
        return 1
    return emit_report(str(project_dir), str(elf_path), sections)


if __name__ == "__main__":
    sys.exit(main())
