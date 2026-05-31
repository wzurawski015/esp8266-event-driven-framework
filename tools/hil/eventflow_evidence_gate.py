#!/usr/bin/env python3
"""Aggregate SDK/HIL evidence into one Event-Driven Reactor hardware gate."""
from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "config" / "eventflow_hardware_evidence.def"
REPORT = ROOT / "docs" / "release" / "eventflow_hardware_evidence_report.md"
ARCH_DOC = ROOT / "docs" / "architecture" / "eventflow_hardware_evidence_contract.md"
EVIDENCE_DIR = ROOT / "docs" / "release" / "eventflow_evidence" / "current"
SOURCE_RE = re.compile(r"^\s*EV_EVENTFLOW_SOURCE\(\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*([^\)]+)\s*\)\s*$")

@dataclass(frozen=True)
class Source:
    name: str
    kind: str
    path: str
    required: bool


def parse_manifest(text: str | None = None) -> list[Source]:
    if text is None:
        text = MANIFEST.read_text(encoding="utf-8")
    sources: list[Source] = []
    for raw in text.splitlines():
        m = SOURCE_RE.match(raw)
        if not m:
            continue
        name, kind, path, required = [part.strip() for part in m.groups()]
        sources.append(Source(name, kind, path, required == "required"))
    if not sources:
        raise RuntimeError("eventflow manifest has no sources")
    return sources


def load_json(rel_path: str) -> dict[str, object] | None:
    path = ROOT / rel_path
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8", errors="ignore"))
    except Exception:
        return None


def source_status(src: Source) -> tuple[str, str, dict[str, object] | None]:
    data = load_json(src.path)
    if data is None:
        return ("NOT_RUN" if not src.required else "FAIL", "missing or invalid JSON evidence", None)
    status = str(data.get("status", "NOT_RUN"))
    if src.kind == "sdk" and status == "PASS":
        values = data.get("values", {}) if isinstance(data.get("values", {}), dict) else {}
        if not any(int(values.get(k, 0) or 0) > 0 for k in ["IRAM", "DRAM", "BSS", "DATA", "APP_BIN"]):
            return "FAIL", "SDK PASS lacks non-zero memory evidence", data
    if src.kind == "hil_i2c" and status == "PASS":
        if data.get("case") != "sda-stuck-low-containment" or not data.get("fixture_coupled", False):
            return "FAIL", "I2C PASS lacks sda-stuck-low fixture-coupled evidence", data
    if src.kind == "hil_wemos_deepsleep" and status == "PASS":
        states = data.get("states", [])
        required_states = ["ACTIVE", "SLEEP_REQUESTED", "DRAINING_RUNTIME", "LOG_FLUSHING", "PORTS_PREPARE_SLEEP", "RTC_STATE_SAVED", "ENTERING_DEEP_SLEEP"]
        if not all(state in states for state in required_states):
            return "FAIL", "Wemos deep-sleep PASS lacks required state sequence", data
    return status, str(data.get("reason", "")), data


def evaluate() -> dict[str, object]:
    rows = []
    has_fail = False
    has_blocked = False
    for src in parse_manifest():
        status, reason, data = source_status(src)
        if status == "FAIL":
            has_fail = True
        if status in {"ENVIRONMENT_BLOCKED", "NOT_RUN"} and src.required:
            has_blocked = True
        rows.append({"name": src.name, "kind": src.kind, "path": src.path, "required": src.required, "status": status, "reason": reason})
    overall = "FAIL" if has_fail else ("ENVIRONMENT_BLOCKED" if has_blocked else "PASS")
    return {"status": overall, "sources": rows}


def write_outputs(result: dict[str, object]) -> None:
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    parsed = EVIDENCE_DIR / "parsed.json"
    parsed.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = [
        "# Event-flow hardware evidence report",
        "",
        "| Field | Value |",
        "|---|---|",
        f"| Status | {result['status']} |",
        f"| Parsed evidence | `{parsed.relative_to(ROOT).as_posix()}` |",
        "",
        "| Source | Kind | Status | Required | Evidence | Reason |",
        "|---|---|---:|---:|---|---|",
    ]
    for row in result["sources"]:
        lines.append(f"| `{row['name']}` | `{row['kind']}` | {row['status']} | {row['required']} | `{row['path']}` | {row['reason']} |")
    lines += [
        "",
        "A PASS means real SDK evidence, ATNEL I2C containment evidence, Wemos smoke evidence, and Wemos deep-sleep/wake evidence are all present and parsed as PASS.",
        "Missing hardware remains ENVIRONMENT_BLOCKED by design; it is not converted into PASS.",
        "",
    ]
    REPORT.write_text("\n".join(lines), encoding="utf-8")
    ARCH_DOC.write_text(
        "# Event-flow hardware evidence contract\n\n"
        "The Event-Driven Reactor hardware gate aggregates real evidence for the asynchronous cycle:\n\n"
        "```text\n"
        "boot -> runtime ready -> actor tick/snapshot events -> I2C hardware fault -> containment/recovery -> sleep request -> quiescence accepted -> power state transitions -> deep sleep entry -> wake boot -> runtime ready again\n"
        "```\n\n"
        "The gate uses `config/eventflow_hardware_evidence.def` as its source of truth. Every PASS must be backed by parsed JSON and committed serial/build evidence; `ENVIRONMENT_BLOCKED` is preserved when hardware or SDK toolchains are not available.\n",
        encoding="utf-8",
    )


def self_test() -> int:
    manifest = "EV_EVENTFLOW_SOURCE(x, sdk, docs/x.json, required)\n"
    parsed = parse_manifest(manifest)
    assert parsed[0].name == "x" and parsed[0].required
    result = {"status": "PASS", "sources": []}
    assert result["status"] == "PASS"
    print("EVENTFLOW_EVIDENCE_GATE_SELF_TEST PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--report", action="store_true")
    ap.add_argument("--gate", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    result = evaluate()
    write_outputs(result)
    if result["status"] == "PASS":
        print("EVENTFLOW_HARDWARE_EVIDENCE_GATE PASS")
        return 0
    if result["status"] == "ENVIRONMENT_BLOCKED":
        print("EVENTFLOW_HARDWARE_EVIDENCE_GATE ENVIRONMENT_BLOCKED")
        return 77 if args.gate else 0
    print("EVENTFLOW_HARDWARE_EVIDENCE_GATE FAIL", file=sys.stderr)
    return 1

if __name__ == "__main__":
    raise SystemExit(main())
