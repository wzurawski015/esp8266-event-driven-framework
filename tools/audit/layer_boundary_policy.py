#!/usr/bin/env python3
"""Conservative Clean Architecture boundary checker.

This audit intentionally focuses on dependency shapes that can be checked with a
fast textual scan: include paths and obvious forbidden symbol families.  Known
legacy actor-driver facades are named exceptions so future true drivers can be
held to the stricter dependency rule without breaking the current compatibility
surface.
"""
from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[2]
INCLUDE_RE = re.compile(r"^\s*#\s*include\s+[<\"](?P<include>[^>\"]+)[>\"]")
C_FILE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}

FORBIDDEN_INCLUDE_TOKENS = {
    "core": ("adapters/", "bsp/", "apps/", "actors/device/", "actors/framework/", "esp8266"),
    "runtime": ("adapters/", "bsp/", "apps/", "esp8266"),
    "ports": ("adapters/", "bsp/", "apps/", "actors/", "esp8266"),
    "drivers": ("adapters/", "bsp/", "apps/", "runtime/", "esp8266"),
}

FORBIDDEN_DRIVER_SYMBOLS = (
    "ev_runtime_",
    "ev_mailbox_",
    "ev_route_table",
    "ev_actor_instance",
    "ev_runtime_graph",
)

LEGACY_DRIVER_FACADE_FILES = {
    "drivers/src/ev_driver_layer.c",
    "drivers/include/ev/ds18b20_actor_driver.h",
    "drivers/include/ev/mcp23008_actor_driver.h",
    "drivers/include/ev/oled_actor_driver.h",
    "drivers/include/ev/panel_actor_driver.h",
    "drivers/include/ev/rtc_actor_driver.h",
}


@dataclass(frozen=True)
class Violation:
    rel: str
    line: int
    reason: str


def relpath(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def layer_for(rel: str) -> str | None:
    for layer in ["core", "runtime", "ports", "drivers", "actors", "adapters", "apps", "bsp", "modules"]:
        if rel == layer or rel.startswith(layer + "/"):
            return layer
    return None


def iter_c_files(root: Path) -> Iterable[Path]:
    for path in root.rglob("*"):
        if any(part in {"build", ".git", "docs/generated", "__pycache__"} for part in path.parts):
            continue
        if path.is_file() and path.suffix in C_FILE_SUFFIXES:
            yield path


def _include_is_forbidden(layer: str, include: str) -> str | None:
    normalized = include.replace("\\", "/")
    for token in FORBIDDEN_INCLUDE_TOKENS.get(layer, ()): 
        if token in normalized:
            return f"{layer} includes forbidden dependency token {token!r} via {include!r}"
    return None


def collect_violations(root: Path = ROOT) -> list[Violation]:
    out: list[Violation] = []
    for path in iter_c_files(root):
        rel = relpath(path, root)
        layer = layer_for(rel)
        if layer is None:
            continue
        try:
            lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        except OSError:
            continue
        legacy_driver_facade = rel in LEGACY_DRIVER_FACADE_FILES
        for line_no, raw in enumerate(lines, 1):
            include_match = INCLUDE_RE.match(raw)
            if include_match is not None:
                reason = _include_is_forbidden(layer, include_match.group("include"))
                if reason is not None:
                    out.append(Violation(rel, line_no, reason))
                if layer == "drivers" and not legacy_driver_facade:
                    inc = include_match.group("include")
                    if inc.endswith("_actor.h") or "/actors/" in inc:
                        out.append(Violation(rel, line_no, f"non-facade driver includes actor header {inc!r}"))
            if layer == "drivers" and not legacy_driver_facade:
                for token in FORBIDDEN_DRIVER_SYMBOLS:
                    if token in raw:
                        out.append(Violation(rel, line_no, f"driver uses runtime/core orchestration symbol {token!r}"))
    return out


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        bad = root / "drivers" / "src" / "bad_sensor.c"
        bad.parent.mkdir(parents=True)
        bad.write_text('#include "ev/rtc_actor.h"\nvoid f(void){ ev_runtime_graph_t *g = 0; (void)g; }\n', encoding="utf-8")
        good = root / "drivers" / "src" / "good_sensor.c"
        good.write_text('#include "ev/port_i2c.h"\nvoid f(void){}\n', encoding="utf-8")
        core_bad = root / "core" / "src" / "bad.c"
        core_bad.parent.mkdir(parents=True)
        core_bad.write_text('#include "adapters/esp8266/foo.h"\n', encoding="utf-8")
        violations = collect_violations(root)
        assert any("bad_sensor.c" in v.rel for v in violations)
        assert any("core/src/bad.c" in v.rel for v in violations)
        assert not any("good_sensor.c" in v.rel for v in violations)
    print("ARCHITECTURE_LAYER_POLICY_SELF_TEST PASS")


def main() -> int:
    parser = argparse.ArgumentParser(description="Check hard layer-boundary invariants.")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    violations = collect_violations(args.root.resolve())
    if violations:
        for item in violations:
            print(f"LAYER_BOUNDARY_VIOLATION {item.rel}:{item.line} {item.reason}")
        print(f"architecture-layer-gate failed violations={len(violations)}")
        return 1
    print("architecture-layer-gate passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
