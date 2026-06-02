#!/usr/bin/env python3
"""Split mixed operator terminal transcripts into clean evidence sublogs.

A mixed transcript is staging input only.  It is never PASS evidence by itself.
Self-tests include the case: whole transcript fails but extracted serial passes.
This tool redacts the transcript, records SHA-256s and source line ranges, then
optionally runs the strict SDK flash/Wemos smoke parsers on extracted sublogs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EVIDENCE_ROOT = ROOT / "docs" / "release" / "operator_transcript_evidence"

PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)
SECRET_ASSIGN_RE = re.compile(
    r"(?P<key>EV_BOARD_NET_WIFI_SSID|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN|WIFI_SSID|WIFI_PASSWORD|COMMAND_TOKEN|SSID|PASSWORD|TOKEN)\s*[=:]\s*(?P<value>[^\s\"']+|\"[^\"]*\"|'[^']*')",
    re.I,
)
SECRET_DEFINE_RE = re.compile(
    r"(?P<prefix>#\s*define\s+(?:EV_BOARD_NET_WIFI_SSID|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\s+)(?P<value>\"[^\"]*\"|\S+)",
    re.I,
)

BUILD_HINT_RE = re.compile(
    r"EV_SDK_BUILD_|make(?:\[[0-9]+\])?: Entering directory .*/targets/|\b(GENCONFIG|DEFCONFIG)\b|\b(?:CC|LD|AR)\s+|App \"|sdk-build",
    re.I,
)
FLASH_HINT_RE = re.compile(r"esptool\.py|Chip is ESP8266EX|Writing at|Hash of data verified|Hard resetting via RTS pin|Leaving\.\.\.", re.I)
SERIAL_HINT_RE = re.compile(r"EV_WEMOS_SMOKE_|EV_POWER_SMOKE_|\bI \([0-9]+\) ev_[A-Za-z0-9_]+:")
SDK_CANONICAL_TARGET_RE = re.compile(r"EV_SDK_BUILD_TARGET=([^\s]+)")
SDK_STATUS_RE = re.compile(r"EV_SDK_BUILD_STATUS=(PASS|FAIL)")
SDK_BEGIN_RE = re.compile(r"EV_SDK_BUILD_BEGIN")
SDK_END_RE = re.compile(r"EV_SDK_BUILD_END")
RESET_FLASH_RE = re.compile(r"Hard resetting via RTS pin|Leaving\.\.\.", re.I)


@dataclass
class Segment:
    kind: str
    start: int  # 0-index inclusive
    end: int    # 0-index exclusive
    path: str
    status: str = "NEEDS_STRICT_IMPORT"
    reason: str = ""
    parser: dict[str, Any] | None = None


def redact(text: str) -> str:
    def repl_assign(match: re.Match[str]) -> str:
        return f"{match.group('key')}=<REDACTED>"

    def repl_define(match: re.Match[str]) -> str:
        return f"{match.group('prefix')}\"<REDACTED>\""

    text = SECRET_DEFINE_RE.sub(repl_define, text)
    text = SECRET_ASSIGN_RE.sub(repl_assign, text)
    return text


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_text(text: str) -> str:
    return sha256_bytes(text.encode("utf-8"))


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def is_placeholder_path(path: Path | str) -> bool:
    return bool(PLACEHOLDER_RE.search(str(path)))


def safe_read_text(path: Path) -> tuple[str, str, str]:
    if is_placeholder_path(path):
        return "ENVIRONMENT_BLOCKED", f"placeholder path was supplied: {path}", ""
    try:
        if not path.exists():
            return "ENVIRONMENT_BLOCKED", f"operator transcript log file not found: {path}", ""
        if path.is_dir():
            return "FAIL", f"input path is directory, expected operator transcript log file: {path}", ""
        return "PASS", "", path.read_text(encoding="utf-8", errors="ignore")
    except PermissionError as exc:
        return "FAIL", f"unable to read operator transcript: {path}: {exc}", ""
    except OSError as exc:
        return "FAIL", f"unable to read operator transcript: {path}: {exc}", ""


def rel_or_name(path: Path, base: Path) -> str:
    try:
        return path.relative_to(base).as_posix()
    except ValueError:
        return path.name


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def contiguous_range(indices: list[int]) -> tuple[int, int] | None:
    if not indices:
        return None
    return min(indices), max(indices) + 1


def classify_segments(lines: list[str], target: str) -> list[Segment]:
    build_indices = [i for i, line in enumerate(lines) if BUILD_HINT_RE.search(line)]
    flash_indices = [i for i, line in enumerate(lines) if FLASH_HINT_RE.search(line)]

    flash_range = contiguous_range(flash_indices)
    flash_end = flash_range[1] if flash_range else 0

    serial_start: int | None = None
    for i, line in enumerate(lines):
        if i < flash_end:
            continue
        if SERIAL_HINT_RE.search(line):
            serial_start = i
            break
    if serial_start is None:
        for i, line in enumerate(lines):
            if SERIAL_HINT_RE.search(line):
                serial_start = i
                break

    segments: list[Segment] = []

    if build_indices:
        build_start = min(build_indices)
        build_end_candidates = [len(lines)]
        if flash_range and flash_range[0] > build_start:
            build_end_candidates.append(flash_range[0])
        if serial_start is not None and serial_start > build_start:
            build_end_candidates.append(serial_start)
        build_end = min(build_end_candidates)
        if build_end > build_start:
            segments.append(Segment("sdk_build", build_start, build_end, "build.log"))

    if flash_range:
        segments.append(Segment("esptool_flash", flash_range[0], flash_range[1], "flash.log"))

    if serial_start is not None:
        segments.append(Segment("wemos_serial", serial_start, len(lines), "serial.raw.log"))

    # Sort and avoid obviously overlapping build/flash segments in manifest consumers.
    segments.sort(key=lambda s: (s.start, s.end, s.kind))
    return segments


def classify_build_segment(text: str, target: str) -> tuple[str, str, dict[str, Any]]:
    markers = SDK_CANONICAL_TARGET_RE.findall(text)
    statuses = SDK_STATUS_RE.findall(text)
    marker_match = any(m == target for m in markers)
    self_test = any(token in text for token in ["target=self-test", "--self-test", "SDK_MEMORY_REPORT_SELF_TEST", "SELF_TEST PASS", "EV_MEM_REPORT_RESULT PASS target=self-test"])
    mixed = self_test or ("EV_MEM_REPORT_RESULT PASS" in text and "EV_SDK_BUILD_STATUS=PASS" not in text)
    data = {
        "canonical_sdk_markers_seen": bool(markers and statuses),
        "target_markers": markers,
        "target_marker_match": marker_match,
        "build_begin_seen": SDK_BEGIN_RE.search(text) is not None,
        "build_end_seen": SDK_END_RE.search(text) is not None,
        "build_statuses": statuses,
        "self_test_marker_seen": self_test,
        "mixed_transcript_detected": mixed,
    }
    if markers and marker_match and "PASS" in statuses and not self_test and not mixed:
        return "NEEDS_STRICT_IMPORT", "canonical SDK markers present; pass to SDK importer/capture gate for strict validation", data
    if not markers:
        return "NEEDS_STRICT_IMPORT", "build segment lacks EV_SDK_BUILD_TARGET/STATUS markers", data
    if not marker_match:
        return "FAIL", f"build segment EV_SDK_BUILD_TARGET does not match expected target {target}", data
    if "PASS" not in statuses:
        return "NEEDS_STRICT_IMPORT", "build segment lacks EV_SDK_BUILD_STATUS=PASS", data
    if self_test or mixed:
        return "FAIL", "build segment contains self-test or mixed-transcript markers", data
    return "NEEDS_STRICT_IMPORT", "SDK build segment requires strict importer validation", data


def run_cmd(cmd: list[str]) -> tuple[str, dict[str, Any]]:
    proc = subprocess.run(cmd, cwd=str(ROOT), text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return ("PASS" if proc.returncode == 0 else ("ENVIRONMENT_BLOCKED" if proc.returncode == 77 else "FAIL")), {
        "argv": cmd,
        "returncode": proc.returncode,
        "stdout_tail": proc.stdout[-2000:],
        "stderr_tail": proc.stderr[-2000:],
    }


def parse_json(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return None


def stage_transcript(*, input_path: Path, target: str, output_dir: Path, run_parsers: bool) -> tuple[int, dict[str, Any]]:
    status, reason, raw_text = safe_read_text(input_path)
    if status == "ENVIRONMENT_BLOCKED":
        return 77, {"status": status, "reason": reason, "target": target}
    if status == "FAIL":
        return 1, {"status": status, "reason": reason, "target": target}

    redacted_text = redact(raw_text)
    output_dir.mkdir(parents=True, exist_ok=True)
    source_path = output_dir / "operator_transcript.raw.log"
    write_text(source_path, redacted_text)
    lines = redacted_text.splitlines()
    source_sha = sha256_file(source_path)

    segments = classify_segments(lines, target)
    manifest_segments: list[dict[str, Any]] = []

    for segment in segments:
        segment_text = "\n".join(lines[segment.start:segment.end]).rstrip() + "\n"
        segment_path = output_dir / segment.path
        write_text(segment_path, segment_text)
        entry: dict[str, Any] = {
            "kind": segment.kind,
            "path": segment.path,
            "sha256": sha256_file(segment_path),
            "source_line_start": segment.start + 1,
            "source_line_end": segment.end,
            "status": segment.status,
            "reason": segment.reason,
        }
        if segment.kind == "sdk_build":
            st, rsn, data = classify_build_segment(segment_text, target)
            entry.update(data)
            entry["status"] = st
            entry["reason"] = rsn
        elif segment.kind == "esptool_flash":
            entry["status"] = "NEEDS_STRICT_IMPORT"
            entry["reason"] = "flash segment extracted; parse_esptool_flash_log.py must validate hash/chip/write proof"
            if run_parsers:
                flash_dir = output_dir / "flash_evidence"
                st, result = run_cmd([sys.executable, "tools/release/parse_esptool_flash_log.py", "--target", target, "--log", str(segment_path), "--evidence-dir", str(flash_dir)])
                parsed = parse_json(flash_dir / "flash_evidence.json") or {}
                entry["parser_status"] = st
                entry["parser_result"] = result
                entry["flash_evidence_path"] = rel_or_name(flash_dir / "flash_evidence.json", output_dir)
                if parsed:
                    entry["status"] = str(parsed.get("status", st))
                    entry["reason"] = "esptool parser result"
                    entry["parser_json"] = parsed
        elif segment.kind == "wemos_serial":
            entry["status"] = "NEEDS_STRICT_IMPORT"
            entry["reason"] = "serial segment extracted; parse_wemos_smoke_log.py must validate marker or runtime-alive fallback proof"
            if run_parsers:
                smoke_dir = output_dir / "wemos_smoke"
                st, result = run_cmd([sys.executable, "tools/hil/parse_wemos_smoke_log.py", "--allow-runtime-alive-fallback", "--normalize", "--log", str(segment_path), "--evidence-dir", str(smoke_dir)])
                parsed = parse_json(smoke_dir / "parsed.json") or {}
                entry["parser_status"] = st
                entry["parser_result"] = result
                entry["wemos_smoke_parsed_path"] = rel_or_name(smoke_dir / "parsed.json", output_dir)
                if parsed:
                    entry["status"] = str(parsed.get("status", st))
                    entry["mode"] = parsed.get("mode")
                    entry["runtime_alive_fallback"] = parsed.get("runtime_alive_fallback")
                    entry["reason"] = "Wemos smoke parser result"
                    if (smoke_dir / "serial.normalized.log").is_file():
                        shutil.copyfile(smoke_dir / "serial.normalized.log", output_dir / "serial.normalized.log")
                        entry["normalized_path"] = "serial.normalized.log"
                        entry["normalized_sha256"] = sha256_file(output_dir / "serial.normalized.log")
                    entry["parser_json"] = parsed
        manifest_segments.append(entry)

    mixed = len([s for s in segments if s.kind in {"sdk_build", "esptool_flash", "wemos_serial"}]) >= 2
    manifest = {
        "status": "PASS" if manifest_segments else "ENVIRONMENT_BLOCKED",
        "evidence_kind": "operator_transcript_staging",
        "source_transcript": source_path.name,
        "source_sha256": source_sha,
        "target": target,
        "mixed_transcript": mixed,
        "line_count": len(lines),
        "segments": manifest_segments,
        "policy": "mixed transcript itself is never PASS evidence; only extracted sublogs may be passed to strict parsers",
    }
    write_text(output_dir / "manifest.json", json.dumps(manifest, indent=2, sort_keys=True) + "\n")

    excerpt = ["# Operator transcript evidence staging excerpt", "", f"Target: `{target}`", f"Source SHA-256: `{source_sha}`", "", "| Segment | Status | Lines | Path | Reason |", "|---|---:|---:|---|---|"]
    for entry in manifest_segments:
        excerpt.append(f"| {entry['kind']} | {entry.get('status','')} | {entry['source_line_start']}-{entry['source_line_end']} | `{entry['path']}` | {entry.get('reason','')} |")
    excerpt.append("\nMixed transcript input is staging material only, not direct PASS evidence.\n")
    write_text(output_dir / "excerpt.md", "\n".join(excerpt))

    sha_lines: list[str] = []
    for path in sorted(output_dir.iterdir()):
        if path.is_file():
            sha_lines.append(f"{sha256_file(path)}  {path.name}")
    write_text(output_dir / "sha256sums.txt", "\n".join(sha_lines) + "\n")
    return 0, manifest


def gate() -> int:
    root = DEFAULT_EVIDENCE_ROOT
    if not root.exists():
        print("OPERATOR_TRANSCRIPT_EVIDENCE_GATE ENVIRONMENT_BLOCKED no operator transcript evidence staged")
        return 77
    manifests = list(root.glob("**/manifest.json"))
    if not manifests:
        print("OPERATOR_TRANSCRIPT_EVIDENCE_GATE ENVIRONMENT_BLOCKED no manifest.json files")
        return 77
    errors: list[str] = []
    for manifest_path in manifests:
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8", errors="ignore"))
        except Exception:
            errors.append(f"invalid manifest: {manifest_path}")
            continue
        if not manifest.get("source_sha256"):
            errors.append(f"manifest lacks source sha256: {manifest_path}")
        for seg in manifest.get("segments", []):
            if not seg.get("sha256") or not seg.get("source_line_start") or not seg.get("source_line_end"):
                errors.append(f"segment lacks sha/range: {manifest_path}:{seg.get('kind')}")
            if seg.get("kind") == "sdk_build" and seg.get("status") == "PASS" and not seg.get("canonical_sdk_markers_seen"):
                errors.append(f"SDK build segment cannot PASS without canonical markers: {manifest_path}")
    if errors:
        for error in errors:
            print("OPERATOR_TRANSCRIPT_EVIDENCE_GATE FAIL " + error, file=sys.stderr)
        return 1
    print("OPERATOR_TRANSCRIPT_EVIDENCE_GATE PASS manifests=" + str(len(manifests)))
    return 0


def self_test() -> int:
    mixed = """./tools/fw sdk-build
make: Entering directory '/repo/adapters/esp8266_rtos_sdk/targets/wemos_esp_wroom_02_18650'
CC core/foo.o
LD ev_wroom_02.elf
esptool.py v3.3
Serial port /dev/ttyUSB0
Chip is ESP8266EX
Writing at 0x00000000... (100 %)
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
EV_WEMOS_SMOKE_TICK seq=10
EV_WEMOS_SMOKE_SNAPSHOT seq=10
EV_WEMOS_SMOKE_TICK seq=11
EV_WEMOS_SMOKE_SNAPSHOT seq=11
EV_WEMOS_SMOKE_TICK seq=12
EV_WEMOS_SMOKE_SNAPSHOT seq=12
COMMAND_TOKEN=OPERATOR_TEST_TOKEN_VALUE_SHOULD_REDACT
"""
    with tempfile.TemporaryDirectory(dir=str(ROOT / "build" if (ROOT / "build").is_dir() else ROOT)) as td:
        d = Path(td)
        log = d / "operator.log"
        log.write_text(mixed, encoding="utf-8")
        rc, manifest = stage_transcript(input_path=log, target="wemos_esp_wroom_02_18650", output_dir=d / "stage", run_parsers=True)
        assert rc == 0
        by_kind = {seg["kind"]: seg for seg in manifest["segments"]}
        assert "sdk_build" in by_kind and by_kind["sdk_build"]["status"] in {"NEEDS_STRICT_IMPORT", "ENVIRONMENT_BLOCKED"}
        assert by_kind["sdk_build"]["status"] != "PASS"
        assert "esptool_flash" in by_kind and by_kind["esptool_flash"]["status"] == "PASS"
        assert "wemos_serial" in by_kind and by_kind["wemos_serial"]["status"] == "PASS"
        assert by_kind["wemos_serial"].get("runtime_alive_fallback") is True
        assert by_kind["wemos_serial"]["source_line_start"] > by_kind["esptool_flash"]["source_line_end"]
        raw = (d / "stage" / "operator_transcript.raw.log").read_text(encoding="utf-8")
        assert "OPERATOR_TEST_TOKEN_VALUE_SHOULD_REDACT" not in raw and "<REDACTED>" in raw
        assert (d / "stage" / "manifest.json").is_file()
        assert (d / "stage" / "sha256sums.txt").is_file()

    whole_status, _reason, whole_text = safe_read_text(log) if False else ("PASS", "", mixed)
    # Whole transcript contains a reset marker; Wemos parser must reject it even with fallback.
    sys.path.insert(0, str(ROOT / "tools" / "hil"))
    import parse_wemos_smoke_log  # type: ignore
    assert parse_wemos_smoke_log.parse_text(whole_text, require_deepsleep=False, allow_runtime_alive_fallback=True)["status"] == "FAIL"
    assert safe_read_text(Path("/path/operator.log"))[0] == "ENVIRONMENT_BLOCKED"

    canonical = """EV_SDK_BUILD_TARGET=wemos_esp_wroom_02_18650
EV_SDK_BUILD_BEGIN
EV_SDK_BUILD_STATUS=PASS
EV_SDK_BUILD_RC=0
EV_SDK_BUILD_END
EV_MEM_APP_BIN=123
"""
    assert classify_build_segment(canonical, "wemos_esp_wroom_02_18650")[0] == "NEEDS_STRICT_IMPORT"
    assert classify_build_segment(canonical.replace("wemos_esp_wroom_02_18650", "other"), "wemos_esp_wroom_02_18650")[0] == "FAIL"
    bad_serial = mixed.replace("EV_WEMOS_SMOKE_TICK seq=11", "panic\nEV_WEMOS_SMOKE_TICK seq=11")
    with tempfile.TemporaryDirectory(dir=str(ROOT / "build" if (ROOT / "build").is_dir() else ROOT)) as td:
        d = Path(td)
        log = d / "operator.log"
        log.write_text(bad_serial, encoding="utf-8")
        rc, manifest = stage_transcript(input_path=log, target="wemos_esp_wroom_02_18650", output_dir=d / "stage", run_parsers=True)
        assert rc == 0
        serial = next(seg for seg in manifest["segments"] if seg["kind"] == "wemos_serial")
        assert serial["status"] == "FAIL"

    print("OPERATOR_TRANSCRIPT_SPLITTER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--input", type=Path)
    ap.add_argument("--target", default="wemos_esp_wroom_02_18650")
    ap.add_argument("--output-dir", type=Path)
    ap.add_argument("--run-parsers", action="store_true")
    ap.add_argument("--gate", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.gate:
        return gate()
    if not args.input:
        print("OPERATOR_TRANSCRIPT_SPLIT ENVIRONMENT_BLOCKED: --input is required")
        return 77
    output_dir = args.output_dir or (DEFAULT_EVIDENCE_ROOT / args.target / "current")
    rc, manifest = stage_transcript(input_path=args.input, target=args.target, output_dir=output_dir, run_parsers=args.run_parsers)
    status = manifest.get("status", "FAIL")
    print(f"OPERATOR_TRANSCRIPT_SPLIT {status} target={args.target} output={output_dir}")
    if status == "ENVIRONMENT_BLOCKED":
        print(f"reason={manifest.get('reason','')}")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
