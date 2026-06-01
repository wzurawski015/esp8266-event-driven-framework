#!/usr/bin/env python3
"""Parse esptool.py flash logs into strict flash evidence.

This parser is intentionally separate from SDK build evidence. A firmware build
PASS is not a flash PASS, and an esptool flash transcript is not a build log.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TARGETS_DEF = ROOT / "config" / "sdk_targets.def"
EVIDENCE_ROOT = ROOT / "docs" / "release" / "sdk_evidence"
TARGET_RE = re.compile(r"^\s*EV_SDK_TARGET\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)")
SECRET_RE = re.compile(r"(WIFI_PASSWORD|COMMAND_TOKEN|EV_BOARD_NET_WIFI_PASSWORD|EV_BOARD_NET_COMMAND_TOKEN)\S*")
PLACEHOLDER_RE = re.compile(r"(^|/)(path|PATH)/(to/)?|<[^>]+>|YOUR_|/path/", re.I)
CHIP_RE = re.compile(r"Chip is\s+(ESP8266EX)", re.I)
WRITE_RE = re.compile(r"Writing at\s+0x[0-9a-fA-F]+|Wrote\s+\d+\s+bytes", re.I)
HASH_RE = re.compile(r"Hash of data verified\.", re.I)
DONE_RE = re.compile(r"Leaving\.\.\.|Hard resetting via RTS pin|Hard resetting", re.I)
FAIL_RE = re.compile(r"\b(?:A fatal error occurred|Traceback \(most recent call last\)|Failed to connect|ERROR|Exception)\b", re.I)
FLASH_REQUIRED_CLASSES = {"physical_smoke", "hil_sdk"}


def redact(text: str) -> str:
    return SECRET_RE.sub(lambda m: m.group(1) + "=<REDACTED>", text)


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def is_placeholder_path(path: Path | str) -> bool:
    return bool(PLACEHOLDER_RE.search(str(path)))


def parse_targets() -> dict[str, dict[str, str]]:
    out: dict[str, dict[str, str]] = {}
    for raw in TARGETS_DEF.read_text(encoding="utf-8").splitlines():
        m = TARGET_RE.match(raw)
        if m:
            name, path, klass, baud, family = [x.strip() for x in m.groups()]
            out[name] = {"target": name, "path": path, "class": klass, "baud": baud, "family": family}
    return out


def safe_read_log(path: Path) -> tuple[str, str, str]:
    if is_placeholder_path(path):
        return "ENVIRONMENT_BLOCKED", f"placeholder path was supplied: {path}", ""
    try:
        if not path.exists():
            return "ENVIRONMENT_BLOCKED", f"flash log file not found: {path}", ""
        if path.is_dir():
            return "FAIL", f"input path is directory, expected flash log file: {path}", ""
        return "PASS", "", path.read_text(encoding="utf-8", errors="ignore")
    except PermissionError as exc:
        return "FAIL", f"unable to read flash log: {path}: {exc}", ""
    except OSError as exc:
        return "FAIL", f"unable to read flash log: {path}: {exc}", ""


def parse_text(text: str, *, target: str) -> dict[str, object]:
    red = redact(text)
    chip_m = CHIP_RE.search(red)
    failures: list[str] = []
    chip = chip_m.group(1).upper() if chip_m else ""
    write_seen = bool(WRITE_RE.search(red))
    hash_verified = bool(HASH_RE.search(red))
    reset_or_leave_seen = bool(DONE_RE.search(red))
    fatal_error = bool(FAIL_RE.search(red))
    if chip != "ESP8266EX":
        failures.append("missing ESP8266EX chip detection marker")
    if not write_seen:
        failures.append("missing write marker")
    if not hash_verified:
        failures.append("missing Hash of data verified marker")
    if not reset_or_leave_seen:
        failures.append("missing Leaving/Hard resetting marker")
    if fatal_error:
        failures.append("fatal error or traceback observed")
    return {
        "evidence_kind": "esptool_flash",
        "target": target,
        "status": "PASS" if not failures else "FAIL",
        "chip_detected": chip,
        "write_seen": write_seen,
        "hash_verified": hash_verified,
        "reset_or_leave_seen": reset_or_leave_seen,
        "fatal_error_seen": fatal_error,
        "failures": failures,
    }


def write_evidence(text: str, *, target: str, evidence_dir: Path) -> int:
    evidence_dir.mkdir(parents=True, exist_ok=True)
    flash_log = evidence_dir / "flash.log"
    flash_log.write_text(redact(text), encoding="utf-8")
    parsed = parse_text(text, target=target)
    parsed.update({"flash_log": flash_log.relative_to(ROOT).as_posix() if flash_log.is_relative_to(ROOT) else str(flash_log), "flash_log_sha256": sha256(flash_log)})
    (evidence_dir / "flash_evidence.json").write_text(json.dumps(parsed, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    (evidence_dir / "flash_excerpt.md").write_text("# esptool flash evidence excerpt\n\n```text\n" + flash_log.read_text(encoding="utf-8")[-4000:] + "\n```\n", encoding="utf-8")
    sha_lines = []
    sha_file = evidence_dir / "sha256sums.txt"
    if sha_file.is_file():
        sha_lines = [line for line in sha_file.read_text(encoding="utf-8", errors="ignore").splitlines() if " flash.log" not in line and " flash_evidence.json" not in line and " flash_excerpt.md" not in line]
    sha_lines.extend([
        f"{sha256(flash_log)}  flash.log",
        f"{sha256(evidence_dir / 'flash_evidence.json')}  flash_evidence.json",
        f"{sha256(evidence_dir / 'flash_excerpt.md')}  flash_excerpt.md",
    ])
    sha_file.write_text("\n".join(sha_lines) + "\n", encoding="utf-8")
    print(f"EV_SDK_FLASH_EVIDENCE {parsed['status']} target={target}")
    return 0 if parsed["status"] == "PASS" else 1


def import_root(root: Path) -> int:
    if is_placeholder_path(root):
        print(f"EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED placeholder import root: {root}")
        return 77
    if not root.is_dir():
        print(f"EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED import root not found: {root}")
        return 77
    imported = 0
    failed = 0
    for target in parse_targets():
        p = root / target / "flash.log"
        if p.is_file():
            status, reason, text = safe_read_log(p)
            if status != "PASS":
                print(f"EV_SDK_FLASH_EVIDENCE {status} target={target} reason={reason}")
                failed += 1
                continue
            rc = write_evidence(text, target=target, evidence_dir=EVIDENCE_ROOT / target)
            imported += 1
            if rc != 0:
                failed += 1
    if imported == 0 and failed == 0:
        print("EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED no target flash.log files in import root")
        return 77
    return 1 if failed else 0


def gate() -> int:
    targets = parse_targets()
    blocked: list[str] = []
    errors: list[str] = []
    for target, meta in targets.items():
        if meta["class"] not in FLASH_REQUIRED_CLASSES:
            continue
        path = EVIDENCE_ROOT / target / "flash_evidence.json"
        if not path.is_file():
            blocked.append(target)
            continue
        try:
            data = json.loads(path.read_text(encoding="utf-8", errors="ignore"))
        except Exception:
            errors.append(f"{target}: invalid flash_evidence.json")
            continue
        if data.get("status") != "PASS":
            errors.append(f"{target}: status={data.get('status')}")
        if data.get("target") != target:
            errors.append(f"{target}: target mismatch in flash evidence")
        if data.get("chip_detected") != "ESP8266EX" or not data.get("hash_verified") or not data.get("write_seen"):
            errors.append(f"{target}: flash evidence lacks ESP8266EX/write/hash proof")
        if not data.get("flash_log_sha256"):
            errors.append(f"{target}: flash evidence lacks flash log SHA-256")
    if errors:
        for error in errors:
            print("EV_SDK_FLASH_EVIDENCE_GATE FAIL " + error, file=sys.stderr)
        return 1
    if blocked:
        print("EV_SDK_FLASH_EVIDENCE_GATE ENVIRONMENT_BLOCKED targets=" + ",".join(blocked))
        return 77
    print("EV_SDK_FLASH_EVIDENCE_GATE PASS")
    return 0


def self_test() -> int:
    valid = """esptool.py v3.3
Serial port /dev/ttyUSB0
Connecting....
Chip is ESP8266EX
MAC: 00:11:22:33:44:55
Writing at 0x00000000... (100 %)
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
"""
    assert parse_text(valid, target="wemos_esp_wroom_02_18650")["status"] == "PASS"
    assert parse_text(valid.replace("Chip is ESP8266EX", ""), target="wemos_esp_wroom_02_18650")["status"] == "FAIL"
    assert parse_text(valid.replace("Hash of data verified.", ""), target="wemos_esp_wroom_02_18650")["status"] == "FAIL"
    assert parse_text(valid + "\nA fatal error occurred", target="wemos_esp_wroom_02_18650")["status"] == "FAIL"
    assert safe_read_log(Path("/path/flash.log"))[0] == "ENVIRONMENT_BLOCKED"
    with tempfile.TemporaryDirectory(dir=str(ROOT / "build" if (ROOT / "build").is_dir() else ROOT)) as td:
        d = Path(td)
        src = d / "flash.log"
        src.write_text(valid + "\nCOMMAND_TOKEN=secret\n", encoding="utf-8")
        assert write_evidence(src.read_text(), target="wemos_esp_wroom_02_18650", evidence_dir=d / "out") == 0
        assert "<REDACTED>" in (d / "out" / "flash.log").read_text(encoding="utf-8")
    print("ESPTOOL_FLASH_LOG_PARSER_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--log", type=Path)
    ap.add_argument("--target")
    ap.add_argument("--evidence-dir", type=Path)
    ap.add_argument("--import-root", type=Path)
    ap.add_argument("--gate", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.gate:
        return gate()
    if args.import_root:
        return import_root(args.import_root)
    if not args.log or not args.target:
        print("EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED: --log and --target are required")
        return 77
    targets = parse_targets()
    if args.target not in targets:
        print(f"EV_SDK_FLASH_EVIDENCE FAIL unknown target: {args.target}", file=sys.stderr)
        return 1
    status, reason, text = safe_read_log(args.log)
    if status == "ENVIRONMENT_BLOCKED":
        print(f"EV_SDK_FLASH_EVIDENCE ENVIRONMENT_BLOCKED target={args.target} reason={reason}")
        return 77
    if status == "FAIL":
        print(f"EV_SDK_FLASH_EVIDENCE FAIL target={args.target} reason={reason}", file=sys.stderr)
        return 1
    return write_evidence(text, target=args.target, evidence_dir=args.evidence_dir or (EVIDENCE_ROOT / args.target))


if __name__ == "__main__":
    raise SystemExit(main())
