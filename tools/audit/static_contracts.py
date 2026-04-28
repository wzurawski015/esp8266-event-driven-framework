#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
errors: list[str] = []

FORBIDDEN_HEAP = re.compile(r"\b(malloc|calloc|realloc|free|strdup)\s*\(")
FORBIDDEN_BLOCK = re.compile(r"\b(portMAX_DELAY|vTaskDelay)\b")
SDK_INCLUDE = re.compile(r'#\s*include\s*[<"](?:esp_|freertos/|FreeRTOS|driver/|gpio|i2c)')
TODO = re.compile(r"\b(TODO|FIXME)\b")

def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text

for subdir in ["core", "runtime", "modules", "drivers", "ports", "apps"]:
    base = ROOT / subdir
    if not base.exists():
        continue
    for p in base.rglob("*"):
        if p.suffix not in {".c", ".h"}:
            continue
        rel = p.relative_to(ROOT).as_posix()
        text = p.read_text(encoding="utf-8", errors="ignore")
        code = strip_comments(text)
        if FORBIDDEN_HEAP.search(code):
            errors.append(f"forbidden heap call in {rel}")
        if subdir in {"core", "runtime", "modules", "drivers", "apps"} and SDK_INCLUDE.search(code):
            errors.append(f"SDK include leak in portable layer {rel}")
        if subdir in {"core", "runtime", "modules", "drivers", "apps"} and FORBIDDEN_BLOCK.search(code):
            errors.append(f"forbidden blocking primitive in {rel}")
        if TODO.search(text):
            errors.append(f"production TODO/FIXME marker in {rel}")

route_lines = [
    line.strip()
    for line in (ROOT / "config" / "routes.def").read_text(encoding="utf-8").splitlines()
    if line.strip().startswith(("EV_ROUTE(", "EV_ROUTE_EX("))
]
gen_h = ROOT / "core" / "generated" / "include" / "ev" / "route_table_generated.h"
if not gen_h.exists():
    errors.append("generated route table header missing")
else:
    match = re.search(r"EV_ROUTE_TABLE_GENERATED_COUNT\s+([0-9]+)U", gen_h.read_text(encoding="utf-8"))
    if match is None or int(match.group(1)) != len(route_lines):
        errors.append("generated route table count does not match config/routes.def")

bsp = ROOT / "bsp"
for board in bsp.iterdir():
    if not board.is_dir():
        continue
    if not (board / "board_profile.h").exists():
        errors.append(f"missing board_profile.h in {board.name}")
    pins = board / "pins.def"
    if pins.exists():
        for line_no, raw in enumerate(pins.read_text(encoding="utf-8", errors="ignore").splitlines(), 1):
            line = raw.strip()
            if line and not line.startswith("//") and not line.startswith(("EV_PIN(", "EV_BSP_PIN(", "EV_BSP_PIN_ANALOG(")):
                errors.append(f"invalid pins.def schema in {board.name}:{line_no}")

for rel in ["config/faults.def", "config/metrics.def", "config/modules.def", "config/capabilities.def"]:
    if not (ROOT / rel).exists():
        errors.append(f"required SSOT file missing: {rel}")

for p in (ROOT / "adapters").rglob("*"):
    if p.suffix not in {".c", ".h"}:
        continue
    text = p.read_text(encoding="utf-8", errors="ignore")
    if ("ISR" in text or "isr" in p.name.lower()) and ("IRAM_ATTR" not in text):
        if re.search(r"\b[a-zA-Z0-9_]*isr[a-zA-Z0-9_]*\s*\(", text, flags=re.I):
            errors.append(f"ISR-like adapter lacks IRAM_ATTR marker: {p.relative_to(ROOT).as_posix()}")
    if re.search(r"\b[a-zA-Z0-9_]*isr[a-zA-Z0-9_]*\s*\([^)]*\)\s*\{[^}]*\b(vTaskDelay|malloc|calloc|realloc|free|printf|EV_LOG)", text, flags=re.I | re.S):
        errors.append(f"ISR-like adapter contains forbidden operation: {p.relative_to(ROOT).as_posix()}")

if (ROOT / "app" / "ev_demo_app.c").exists():
    errors.append("legacy app/ev_demo_app.c remains outside apps/demo")

if errors:
    for error in errors:
        print(error)
    raise SystemExit(1)

print("static contracts passed")
