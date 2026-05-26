#!/usr/bin/env python3
"""ESP8266 SDK ELF, binary-size and stack-usage memory report helper.

The tool intentionally reports only information that can be derived from build
artifacts. It does not parse compiler command lines and does not print build
flags, so local WiFi credentials passed through private build configuration are
not leaked into the report.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
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


@dataclass(frozen=True)
class StackUsageSummary:
    status: str
    files: int
    entries: int
    max_frame: int
    function: str
    qualifier: str
    source: str


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


def find_app_bin_for_elf(elf_path: Path, project_dir: Optional[Path]) -> Optional[Path]:
    """Return the application .bin that deterministically corresponds to ELF.

    ESP8266 RTOS SDK projects emit the application binary next to the top-level
    application ELF as build/<PROJECT_NAME>.bin. We intentionally avoid broad
    rglob guesses: if that exact basename relation is absent, the binary size is
    reported as not_available rather than guessed from unrelated bootloader or
    partition-table artifacts.
    """

    candidates = []
    direct = elf_path.with_suffix(".bin")
    candidates.append(direct)
    if project_dir is not None:
        candidates.append(project_dir / "build" / f"{elf_path.stem}.bin")

    seen: set[Path] = set()
    for candidate in candidates:
        resolved = candidate.resolve() if candidate.exists() else candidate.absolute()
        if resolved in seen:
            continue
        seen.add(resolved)
        if candidate.is_file():
            return candidate
    return None


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


def source_token(path: Optional[Path], fallback: str) -> str:
    if path is None:
        return fallback
    return path.as_posix()


def discover_stack_usage_files(project_dir: Optional[Path], elf_path: Optional[Path]) -> List[Path]:
    roots: List[Path] = []
    if project_dir is not None:
        roots.append(project_dir / "build")
    if elf_path is not None:
        roots.append(elf_path.parent)

    files: List[Path] = []
    seen: set[Path] = set()
    for root in roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*.su"):
            if "bootloader" in path.parts:
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            files.append(path)
    files.sort()
    return files


def parse_stack_usage_line(line: str) -> Optional[Tuple[str, int, str]]:
    stripped = line.strip()
    if not stripped:
        return None
    parts = stripped.split()
    if len(parts) < 3:
        return None
    try:
        frame_bytes = int(parts[-2], 10)
    except ValueError:
        return None
    qualifier = parts[-1]
    location = " ".join(parts[:-2])
    function = location.rsplit(":", 1)[-1].strip() if ":" in location else location.strip()
    if not function:
        function = "unknown"
    return function, frame_bytes, qualifier


def parse_stack_usage_files(files: Iterable[Path]) -> StackUsageSummary:
    file_list = list(files)
    if not file_list:
        return StackUsageSummary(
            status="not_available",
            files=0,
            entries=0,
            max_frame=0,
            function="unknown",
            qualifier="unknown",
            source="not_found",
        )

    entries = 0
    max_frame = 0
    max_function = "unknown"
    max_qualifier = "unknown"
    max_source = file_list[0]

    for path in file_list:
        for raw in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            parsed = parse_stack_usage_line(raw)
            if parsed is None:
                continue
            function, frame_bytes, qualifier = parsed
            entries += 1
            if frame_bytes >= max_frame:
                max_frame = frame_bytes
                max_function = function
                max_qualifier = qualifier
                max_source = path

    if entries == 0:
        return StackUsageSummary(
            status="not_available",
            files=len(file_list),
            entries=0,
            max_frame=0,
            function="unknown",
            qualifier="unknown",
            source=source_token(file_list[0], "not_found"),
        )

    return StackUsageSummary(
        status="ok",
        files=len(file_list),
        entries=entries,
        max_frame=max_frame,
        function=max_function,
        qualifier=max_qualifier,
        source=source_token(max_source, "not_found"),
    )


def stack_status_for_limit(summary: StackUsageSummary, limit: Optional[int]) -> Tuple[str, str, int]:
    if summary.status == "not_available":
        if limit is None:
            return "not_available", "unchecked", 0
        return "not_available", str(limit), 1
    if limit is None:
        return "unchecked", "unchecked", 0
    if summary.max_frame > limit:
        return "fail", str(limit), 1
    return "ok", str(limit), 0


def emit_report(
    target: str,
    elf: str,
    sections: Dict[str, int],
    *,
    project_dir: Optional[Path] = None,
    elf_path: Optional[Path] = None,
    app_bin_path: Optional[Path] = None,
    stack_usage_files: Optional[List[Path]] = None,
) -> int:
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

    if app_bin_path is None and elf_path is not None:
        app_bin_path = find_app_bin_for_elf(elf_path, project_dir)
    app_bin_limit_env = env_int("EV_SDK_MAX_APP_BIN_BYTES")
    app_bin_size = 0
    if app_bin_path is None:
        app_bin_status = "not_available"
        app_bin_limit = "unchecked" if app_bin_limit_env is None else str(app_bin_limit_env)
        app_bin_source = "not_found"
        if app_bin_limit_env is not None:
            failures += 1
    else:
        app_bin_size = app_bin_path.stat().st_size
        app_bin_status, app_bin_limit, _app_bin_free, app_bin_fail = status_for_limit(app_bin_size, app_bin_limit_env)
        app_bin_source = source_token(app_bin_path, "not_found")
        failures += app_bin_fail

    if stack_usage_files is None:
        stack_usage_files = discover_stack_usage_files(project_dir, elf_path)
    stack_summary = parse_stack_usage_files(stack_usage_files)
    stack_status, stack_limit, stack_fail = stack_status_for_limit(stack_summary, env_int("EV_SDK_MAX_STACK_FRAME_BYTES"))
    failures += stack_fail

    print(f"EV_MEM_REPORT_START target={target} elf={elf}")
    for name in sorted(sections):
        print(f"EV_MEM_SECTION name={name} size={sections[name]}")
    print(f"EV_MEM_IRAM used={iram_used} limit={iram_limit} free={iram_free} status={iram_status}")
    print(f"EV_MEM_DRAM used={dram_used} limit={dram_limit} free={dram_free} status={dram_status}")
    print(f"EV_MEM_BSS size={bss_size} limit={bss_limit} status={bss_status}")
    print(f"EV_MEM_DATA size={data_size} limit={data_limit} status={data_status}")
    print(
        f"EV_MEM_APP_BIN size={app_bin_size} limit={app_bin_limit} "
        f"status={app_bin_status} source={app_bin_source}"
    )
    print(
        f"EV_MEM_STACK_USAGE status={stack_status} files={stack_summary.files} "
        f"entries={stack_summary.entries} max_frame={stack_summary.max_frame} "
        f"limit={stack_limit} function={stack_summary.function} "
        f"qualifier={stack_summary.qualifier} source={stack_summary.source}"
    )
    print("EV_MEM_STACK status=not_available source=legacy_marker_use_EV_MEM_STACK_USAGE")

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
    assert parse_stack_usage_line("main.c:10:3:app_main\t96\tstatic") == ("app_main", 96, "static")
    assert parse_stack_usage_line("driver.c:42:7:handler 128 dynamic,bounded") == ("handler", 128, "dynamic,bounded")

    old_env = dict(os.environ)
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        app_bin = tmp_path / "self-test.bin"
        app_bin.write_bytes(b"x" * 1024)
        stack_file = tmp_path / "self-test.su"
        stack_file.write_text(
            "main.c:10:3:app_main\t96\tstatic\n"
            "driver.c:42:7:handler\t128\tdynamic,bounded\n",
            encoding="utf-8",
        )
        try:
            os.environ["EV_SDK_IRAM_LIMIT_BYTES"] = "1200"
            os.environ["EV_SDK_DRAM_LIMIT_BYTES"] = "768"
            os.environ["EV_SDK_MAX_APP_BIN_BYTES"] = "1024"
            os.environ["EV_SDK_MAX_STACK_FRAME_BYTES"] = "128"
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=app_bin,
                stack_usage_files=[stack_file],
            ) == 0
            os.environ["EV_SDK_MAX_BSS_BYTES"] = "511"
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=app_bin,
                stack_usage_files=[stack_file],
            ) == 1
            os.environ["EV_SDK_MAX_BSS_BYTES"] = "512"
            os.environ["EV_SDK_MAX_APP_BIN_BYTES"] = "1023"
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=app_bin,
                stack_usage_files=[stack_file],
            ) == 1
            os.environ["EV_SDK_MAX_APP_BIN_BYTES"] = "1024"
            os.environ["EV_SDK_MAX_STACK_FRAME_BYTES"] = "127"
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=app_bin,
                stack_usage_files=[stack_file],
            ) == 1
            del os.environ["EV_SDK_MAX_STACK_FRAME_BYTES"]
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=None,
                stack_usage_files=[],
            ) == 1
            del os.environ["EV_SDK_MAX_APP_BIN_BYTES"]
            assert emit_report(
                "self-test",
                "self-test.elf",
                sections,
                app_bin_path=None,
                stack_usage_files=[],
            ) == 0
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

    project_dir: Optional[Path]
    elf_path: Optional[Path]
    if args.size_output:
        size_text = Path(args.size_output).read_text(encoding="utf-8")
        sections = parse_size_output(size_text)
        project_dir = Path(args.project_dir) if args.project_dir else None
        elf_path = Path(args.elf) if args.elf else None
        target = args.project_dir or "size-output"
        elf = args.elf or args.size_output
        return emit_report(target, elf, sections, project_dir=project_dir, elf_path=elf_path)

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
    return emit_report(str(project_dir), str(elf_path), sections, project_dir=project_dir, elf_path=elf_path)


if __name__ == "__main__":
    sys.exit(main())
